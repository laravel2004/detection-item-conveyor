#include "yolo_detector.hpp"

#include <dml_provider_factory.h>
#include <onnxruntime_cxx_api.h>

#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <utility>

struct YoloDetector::Implementation {
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "sack-rtsp"};
    Ort::SessionOptions sessionOptions;
    std::unique_ptr<Ort::Session> session;
    std::string inputName;
    std::string outputName;
    std::vector<float> inputTensor;
    std::string device;
    float confidenceThreshold = 0.10F;
    float nmsThreshold = 0.45F;
    int inputSize = 640;
};

YoloDetector::YoloDetector(const std::filesystem::path& modelPath,
                           std::string device,
                           float confidenceThreshold,
                           float nmsThreshold,
                           int inputSize)
    : implementation_(std::make_unique<Implementation>()) {
    auto& state = *implementation_;
    state.device = std::move(device);
    state.confidenceThreshold = confidenceThreshold;
    state.nmsThreshold = nmsThreshold;
    state.inputSize = inputSize;
    state.inputTensor.resize(
        static_cast<std::size_t>(3) * inputSize * inputSize);
    state.sessionOptions.SetGraphOptimizationLevel(
        GraphOptimizationLevel::ORT_ENABLE_ALL);

    if (state.device != "cpu") {
        state.sessionOptions.DisableMemPattern();
        state.sessionOptions.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(
            state.sessionOptions, 0));
        std::cout << "Backend inferensi: ONNX Runtime DirectML GPU\n";
    } else {
        std::cout << "Backend inferensi: ONNX Runtime CPU\n";
    }

    state.session = std::make_unique<Ort::Session>(
        state.environment, modelPath.c_str(), state.sessionOptions);
    Ort::AllocatorWithDefaultOptions allocator;
    const auto inputName = state.session->GetInputNameAllocated(0, allocator);
    const auto outputName = state.session->GetOutputNameAllocated(0, allocator);
    state.inputName = inputName.get();
    state.outputName = outputName.get();
}

YoloDetector::~YoloDetector() = default;
YoloDetector::YoloDetector(YoloDetector&&) noexcept = default;
YoloDetector& YoloDetector::operator=(YoloDetector&&) noexcept = default;

std::vector<Detection> YoloDetector::detect(const cv::Mat& frame) {
    auto& state = *implementation_;
    const float scale = std::min(
        static_cast<float>(state.inputSize) / static_cast<float>(frame.cols),
        static_cast<float>(state.inputSize) / static_cast<float>(frame.rows));
    const int resizedWidth = static_cast<int>(std::round(frame.cols * scale));
    const int resizedHeight = static_cast<int>(std::round(frame.rows * scale));
    const int left = (state.inputSize - resizedWidth) / 2;
    const int top = (state.inputSize - resizedHeight) / 2;

    cv::Mat resized;
    cv::resize(frame, resized, {resizedWidth, resizedHeight});
    cv::Mat letterboxed(
        state.inputSize, state.inputSize, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(letterboxed(cv::Rect(left, top, resizedWidth, resizedHeight)));

    cv::Mat rgbFloat;
    cv::cvtColor(letterboxed, rgbFloat, cv::COLOR_BGR2RGB);
    rgbFloat.convertTo(rgbFloat, CV_32F, 1.0 / 255.0);
    std::vector<cv::Mat> channels;
    cv::split(rgbFloat, channels);
    const std::size_t planeSize =
        static_cast<std::size_t>(state.inputSize) * state.inputSize;
    for (std::size_t channel = 0; channel < channels.size(); ++channel) {
        std::memcpy(state.inputTensor.data() + channel * planeSize,
                    channels[channel].ptr<float>(), planeSize * sizeof(float));
    }

    const std::array<int64_t, 4> inputShape{
        1, 3, state.inputSize, state.inputSize};
    auto memoryInfo = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator, OrtMemTypeDefault);
    auto inputTensor = Ort::Value::CreateTensor<float>(
        memoryInfo,
        state.inputTensor.data(),
        state.inputTensor.size(),
        inputShape.data(),
        inputShape.size());
    const char* inputNames[] = {state.inputName.c_str()};
    const char* outputNames[] = {state.outputName.c_str()};
    auto outputs = state.session->Run(
        Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1, outputNames, 1);
    if (outputs.size() != 1 || !outputs.front().IsTensor()) {
        throw std::runtime_error("Output ONNX bukan tensor tunggal.");
    }
    const auto outputShape =
        outputs.front().GetTensorTypeAndShapeInfo().GetShape();
    if (outputShape.size() != 3 || outputShape[0] != 1) {
        throw std::runtime_error("Bentuk output ONNX tidak didukung.");
    }
    const int attributes = static_cast<int>(outputShape[1]);
    const int predictionCount = static_cast<int>(outputShape[2]);
    if (attributes < 5) {
        throw std::runtime_error("Output ONNX tidak memiliki kolom kelas.");
    }
    cv::Mat rows(attributes, predictionCount, CV_32F,
                 outputs.front().GetTensorMutableData<float>());
    rows = rows.t();

    std::vector<cv::Rect> boxes;
    std::vector<float> scores;
    boxes.reserve(predictionCount);
    scores.reserve(predictionCount);
    for (int rowIndex = 0; rowIndex < rows.rows; ++rowIndex) {
        const float* row = rows.ptr<float>(rowIndex);
        float score = row[4];
        for (int classIndex = 5; classIndex < attributes; ++classIndex) {
            score = std::max(score, row[classIndex]);
        }
        if (score < state.confidenceThreshold) {
            continue;
        }
        const float centerX = (row[0] - static_cast<float>(left)) / scale;
        const float centerY = (row[1] - static_cast<float>(top)) / scale;
        const float width = row[2] / scale;
        const float height = row[3] / scale;
        int x = static_cast<int>(std::round(centerX - width * 0.5F));
        int y = static_cast<int>(std::round(centerY - height * 0.5F));
        int right = static_cast<int>(std::round(centerX + width * 0.5F));
        int bottom = static_cast<int>(std::round(centerY + height * 0.5F));
        x = std::clamp(x, 0, frame.cols - 1);
        y = std::clamp(y, 0, frame.rows - 1);
        right = std::clamp(right, x + 1, frame.cols);
        bottom = std::clamp(bottom, y + 1, frame.rows);
        boxes.emplace_back(x, y, right - x, bottom - y);
        scores.push_back(score);
    }

    std::vector<int> keptIndices;
    cv::dnn::NMSBoxes(boxes, scores, state.confidenceThreshold,
                      state.nmsThreshold, keptIndices);
    std::vector<Detection> detections;
    detections.reserve(keptIndices.size());
    for (const int index : keptIndices) {
        Detection detection;
        detection.box = boxes[static_cast<std::size_t>(index)];
        detection.confidence = scores[static_cast<std::size_t>(index)];
        detection.appearance = appearanceDescriptor(frame, detection.box);
        detections.push_back(std::move(detection));
    }
    return detections;
}