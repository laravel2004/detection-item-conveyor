#include "report_paths.hpp"
#include "stable_tracker.hpp"

#include <cassert>
#include <iostream>

namespace {

Detection detection(float centerY,
                    float confidence = 0.9F,
                    float centerX = 50.0F,
                    cv::Mat appearance = {}) {
    return {{centerX - 25.0F, centerY - 50.0F, 50.0F, 100.0F},
            confidence,
            std::move(appearance)};
}

}  // namespace

int main() {
    assert(reportSourceKey(
               "rtsp://admin:password@192.168.199.110:554/Streaming/Channels/101") ==
           "192.168.199.110");
    assert(reportSourceKey("rtsps://user@[2001:db8::1]:7441/live") ==
           "2001_db8__1");
    assert(reportSourceKey("../sample-video/video1.mp4") == "video1");

    {
        StableTracker tracker;
        double now = 0.0;
        int stableTrackId = 0;
        for (const float y : {50.0F, 55.0F, 60.0F}) {
            now += 0.1;
            const auto result = tracker.update({detection(y)}, now, 68, 88, 0, 100);
            assert(result.assignments[0]);
            assert(result.assignments[0]->sackId == 0);
            stableTrackId = result.assignments[0]->trackId;
        }
        now += 1.2;
        const auto recovered = tracker.update({detection(120.0F)}, now, 68, 88, 0, 100);
        assert(recovered.assignments[0]);
        assert(recovered.assignments[0]->trackId == stableTrackId);
        assert(recovered.assignments[0]->sackId == 1);

        now += 0.1;
        const auto counted = tracker.update({detection(125.0F)}, now, 68, 88, 0, 100);
        assert(counted.assignments[0]);
        assert(counted.assignments[0]->counted);
        assert(counted.assignments[0]->sackId == 1);
        assert(tracker.total() == 1);
    }

    {
        StableTracker tracker;
        double now = 0.0;
        int firstId = 0;
        int secondId = 0;
        for (const float y : {55.0F, 70.0F, 80.0F, 92.0F, 96.0F,
                              105.0F, 115.0F, 120.0F}) {
            now += 0.1;
            const auto result = tracker.update(
                {detection(y, 0.9F, 45.0F), detection(y - 15.0F, 0.9F, 85.0F)},
                now, 68, 88, 0, 130);
            assert(result.assignments[0]);
            assert(result.assignments[1]);
            firstId = result.assignments[0]->trackId;
            secondId = result.assignments[1]->trackId;
            assert(firstId != secondId);
        }
        assert(tracker.total() == 2);
    }

    {
        StableTracker tracker;
        cv::Mat firstAppearance = cv::Mat::zeros(1, 4, CV_32F);
        cv::Mat secondAppearance = cv::Mat::zeros(1, 4, CV_32F);
        firstAppearance.at<float>(0, 0) = 1.0F;
        secondAppearance.at<float>(0, 1) = 1.0F;
        const auto first = tracker.update(
            {detection(60.0F, 0.9F, 50.0F, firstAppearance)}, 0.1, 68, 88, 0, 100);
        const auto second = tracker.update(
            {detection(62.0F, 0.9F, 50.0F, secondAppearance)}, 0.2, 68, 88, 0, 100);
        assert(first.assignments[0]);
        assert(second.assignments[0]);
        assert(first.assignments[0]->trackId != second.assignments[0]->trackId);
        assert(first.assignments[0]->sackId == 0);
        assert(second.assignments[0]->sackId == 0);
    }

    {
        StableTracker tracker;
        const auto weak = tracker.update({detection(60.0F, 0.20F)}, 0.1,
                                         68, 88, 0, 100);
        assert(!weak.assignments[0]);
        assert(tracker.total() == 0);
    }

    {
        // Satu frame di bawah line 2 langsung cukup untuk konfirmasi count.
        StableTracker tracker;
        double now = 0.0;
        for (const float centerY : {50.0F, 65.0F, 70.0F}) {
            now += 0.1;
            tracker.update({detection(centerY)}, now, 68, 92, 0, 100);
        }
        now += 0.1;
        const auto counted = tracker.update(
            {detection(96.0F)}, now, 68, 92, 0, 100);
        assert(counted.assignments[0]);
        assert(counted.assignments[0]->counted);
        assert(tracker.total() == 1);
    }

    {
        // Skenario konveyor berhenti 8 detik di checkpoint, lalu lanjut jalan melintasi Line 2
        StableTracker tracker;
        double now = 0.0;
        int originalTrackId = 0;
        int originalSackId = 0;

        // Karung masuk checkpoint (Y=50 -> 60 -> 75 di antara line 68 dan 92)
        for (const float centerY : {50.0F, 60.0F, 75.0F}) {
            now += 0.1;
            const auto res = tracker.update({detection(centerY)}, now, 68, 92, 0, 100);
            assert(res.assignments[0]);
            originalTrackId = res.assignments[0]->trackId;
            originalSackId = res.assignments[0]->sackId;
        }
        assert(originalSackId > 0);
        assert(tracker.total() == 0);

        // Konveyor mati 8 detik, karung tetap di Y=75
        now += 8.0;
        const auto stoppedRes = tracker.update({detection(75.0F)}, now, 68, 92, 0, 100);
        assert(stoppedRes.assignments[0]);
        assert(stoppedRes.assignments[0]->trackId == originalTrackId);
        assert(stoppedRes.assignments[0]->sackId == originalSackId);
        assert(!stoppedRes.assignments[0]->counted); // Belum terhitung
        assert(tracker.total() == 0);

        // Konveyor nyala lagi dan membawa karung melintasi Line 2 (Y=96)
        now += 0.2;
        const auto finishRes = tracker.update({detection(96.0F)}, now, 68, 92, 0, 100);
        assert(finishRes.assignments[0]);
        assert(finishRes.assignments[0]->trackId == originalTrackId);
        assert(finishRes.assignments[0]->sackId == originalSackId);
        assert(finishRes.assignments[0]->counted); // Baru terhitung sah
        assert(tracker.total() == 1);
    }

    {
        // Skenario osilasi/getaran di dekat Garis 2 tanpa pergerakan translasi (tidak boleh count)
        StableTracker tracker;
        double now = 0.0;
        // Objek spawn langsung di dekat garis bawah (Y=90) dan berosilasi 90 -> 93 -> 90 -> 93
        for (int i = 0; i < 6; ++i) {
            now += 0.1;
            const float y = (i % 2 == 0) ? 90.0F : 93.0F;
            tracker.update({detection(y)}, now, 68, 92, 0, 100);
        }
        // Karena tidak pernah melewati checkpoint atas (seenAboveCount = false) dan delta Y < min
        assert(tracker.total() == 0);
    }

    {
        // Skenario Anti-Degradasi: Karung di checkpoint tidak boleh kembali menjadi NEW
        StableTracker tracker;
        double now = 0.0;

        // Masuk checkpoint di Y=75
        now += 0.1;
        auto step1 = tracker.update({detection(75.0F)}, now, 68, 92, 0, 100);
        assert(step1.assignments[0]);
        assert(step1.assignments[0]->pending);
        assert(step1.assignments[0]->sackId == 1);

        // Frame berikutnya deteksi berubah bentuk (misal ukuran menyusut 45%)
        now += 0.1;
        Detection deformedBox{{25.0F, 50.0F, 30.0F, 60.0F}, 0.85F, {}};
        auto step2 = tracker.update({deformedBox}, now, 68, 92, 0, 100);
        assert(step2.assignments[0]);
        // Pastikan TIDAK kembali ke NEW (pending harus tetap true dan sackId tetap 1)
        assert(step2.assignments[0]->pending);
        assert(step2.assignments[0]->sackId == 1);
    }

    {
        // Skenario Fast Moving: Karung meluncur cepat hanya dalam 2 frame
        StableTracker tracker;
        double now = 0.0;

        now += 0.05;
        auto f1 = tracker.update({detection(72.0F)}, now, 68, 92, 0, 100);
        assert(f1.assignments[0]);
        assert(f1.assignments[0]->pending);

        now += 0.05;
        auto f2 = tracker.update({detection(96.0F)}, now, 68, 92, 0, 100);
        assert(f2.assignments[0]);
        assert(f2.assignments[0]->counted);
        assert(tracker.total() == 1);
    }

    {
        // Skenario Karung di Tepi Konveyor (X di luar batas sempit 100, misal X=110)
        StableTracker tracker;
        double now = 0.0;

        now += 0.1;
        auto e1 = tracker.update({detection(70.0F, 0.9F, 110.0F)}, now, 68, 92, 0, 100);
        assert(e1.assignments[0]);
        assert(e1.assignments[0]->pending);

        now += 0.1;
        auto e2 = tracker.update({detection(96.0F, 0.9F, 110.0F)}, now, 68, 92, 0, 100);
        assert(e2.assignments[0]);
        assert(e2.assignments[0]->counted);
        assert(tracker.total() == 1);
    }

    std::cout << "stable_tracker_test OK\n";
    return 0;
}
