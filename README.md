# C++ RTSP Sack Detection

Implementasi native C++17 dari `show_detection_rtsp.py`. Program membuka RTSP
melalui TCP, menjalankan `best.onnx`, mempertahankan `sack_id` ketika deteksi
hilang sesaat, menghitung setelah dua garis, menulis report per jam, menampilkan
preview, dan menyimpan video beranotasi.

## Build

Visual C++ Build Tools dan OpenCV lokal harus sudah terpasang. Dari PowerShell:

```powershell
cd cpp_rtsp
.\build.ps1
```

## Menjalankan

```powershell
.\build\Release\sack_rtsp.exe --source "rtsp://user:password@ip:554/path" --device gpu
```

Pilihan lain:

- `--model <path>` memilih model ONNX lain (default otomatis mendeteksi `best.onnx` di root folder).
- `--device gpu` memakai ONNX Runtime DirectML pada GPU DirectX 12. Alias
  `--device cuda` dan `--device 0` juga diterima.
- `--device cpu` memakai ONNX Runtime CPU.
- `--display-width N` mengatur lebar awal jendela preview (default `1280` / `1600`).
- `--no-save` tidak menyimpan video.
- `--no-display` berjalan tanpa jendela preview (headless).
- `--max-frames N` berhenti setelah N frame, berguna untuk pengujian.

### Kontrol Keyboard (Interaktif saat Preview Berjalan)

- **`Q` / `ESC`** : Keluar dari program secara aman.
- **`H`** : Sembunyikan (*hide*) jendela preview agar berjalan di latar belakang tanpa beban render GUI, atau tampilkan kembali (*unhide*).
- **`M`** : Me-*minimize* jendela ke taskbar Windows.
- **`F`** : Mode layar penuh (*toggle fullscreen*).
- **Resize Mouse** : Tepi jendela dapat ditarik (*drag*) untuk memperbesar/memperkecil jendela secara bebas. Video otomatis diskalakan proporsional (*letterbox fit*) tanpa pernah memunculkan scrollbar.

Model default adalah `best.onnx` di root folder `cpp_rtsp`.
Output video berada di `../runs/detect`. Report disimpan berdasarkan tanggal dan
IP/host RTSP dengan struktur berikut:

```text
cpp_rtsp/report/<tahun>/<bulan>/<hari>/<IP-atau-host-RTSP>/rtsp_sack_count_<jam>.txt
```

Contoh:

```text
cpp_rtsp/report/2026/09/22/192.168.199.110/rtsp_sack_count_0900.txt
```

Username, password, port, dan path RTSP tidak dimasukkan ke nama folder report.

## Performa

Rekaman tetap memakai resolusi asli kamera. Preview dibatasi ke lebar 1600
secara default, sedangkan hanya salinan untuk inferensi yang di-letterbox ke
640x640. Gunakan `--display-width 1280` bila ukuran jendela masih terlalu besar,
atau `--display-width 0` untuk preview asli. Pada RTX 3060, pengujian lokal
500 frame mencapai sekitar 50 FPS tanpa rekaman dan sekitar 33 FPS sambil
menulis MP4 1920x1080. FPS RTSP sebenarnya juga dipengaruhi jaringan, decoder,
bitrate kamera, dan codec stream.

Untuk RTSP, pembacaan frame berjalan pada thread tersendiri agar penerimaan
paket kamera tidak menunggu inferensi GPU. Overlay menampilkan `FPS` pemrosesan
dan `SOURCE` FPS yang dilaporkan kamera. Jika keduanya stabil di sekitar 20,
stream kamera memang dikonfigurasi 20 FPS dan harus dinaikkan dari pengaturan
video kamera untuk memperoleh frame asli lebih banyak.

Deteksi dengan confidence di bawah 0.30 tidak digambar. Deteksi confidence
0.10-0.29 tetap dipakai internal oleh tracker untuk mempertahankan `sack_id`
ketika sack blur atau tertutup sesaat.
