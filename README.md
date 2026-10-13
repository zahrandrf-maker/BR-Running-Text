# BR Running Text — V0.4.1 Beta

Source FFGL transparan untuk Resolume Arena/Avenue di Windows. Dibuat oleh
**BELAJAR RESOLUME**.

## Fitur beta

- Running text horizontal dan credit multiline vertikal.
- Static lower third, rotasi 0/90/180/270°, dan bounce.
- Membaca font yang terpasang di Windows serta input nama font manual.
- Warna, opacity, padding, ukuran otomatis/manual, dan rounded box.
- Stroke teks dan stroke box dengan warna, ketebalan, posisi, serta offset.
- Flicker sequential/random/all dan wiggle.
- Animation In/Out Scroll X/Y, invert, manual atau auto timer.
- Output alpha/transparan untuk ditempatkan pada layer tersendiri.

## Build melalui GitHub

1. Buat repository GitHub kosong.
2. Upload **semua isi folder ini**, termasuk folder `.github`.
3. Buka tab **Actions** lalu pilih **Build BR Running Text**.
4. Klik **Run workflow**.
5. Setelah selesai, buka job build dan unduh artifact
`BR-Running-Text-Windows-v0.4.1-beta`.

Tidak perlu menginstal Visual Studio di komputer Anda. GitHub Actions akan
mengambil FFGL SDK resmi dan membangun DLL Windows x64.

## Instalasi

1. Tutup Resolume.
2. Salin `BR Running Text.dll` ke folder plugin FFGL Resolume Anda.
3. Buka Resolume kembali.
4. Buka panel **Sources** (bukan Effects), lalu cari **BR Running Text**.
5. Tarik source ke sebuah clip pada layer tersendiri.

Lokasi plugin yang umum:

`Documents\\Resolume Arena\\Extra Effects`

Jika plugin belum terlihat, periksa lokasi plugin pada Preferences Resolume.
Pastikan file tidak masih berada di dalam ZIP dan restart Resolume setelah
menyalin DLL.

## Cara penggunaan

- `Continuous Repeat` mengulang teks tanpa putus dan merupakan mode default.
- `Show Separator |` menampilkan atau menyembunyikan pemisah antar pengulangan.
- `Continuous Loop Gap` mengatur jarak antar salinan teks pada mode Continuous.
- `Single Repeat` menampilkan satu salinan, membiarkannya keluar seluruhnya, lalu mengulang.
- Klik `Edit Text` untuk membuka editor teks besar yang dapat di-resize.
- Di dalam editor, Enter selalu membuat baris baru dan tidak memperbarui output.
- Klik `Update Text` untuk menerapkan perubahan, atau `Cancel` untuk membatalkan.
- `Text Preview / Preset` mempertahankan kompatibilitas penyimpanan teks pada preset Resolume.
- Tanpa Enter: teks dianggap sebagai running text satu baris.
- Dengan Enter: teks menjadi multiline untuk credit Up/Down.
- Motion `Static`: digunakan sebagai lower third.
- Animation In/Out dalam keadaan Off secara default.
- `Reset Position` mengulang waktu motion dan animasi.
- `Play Out` menjalankan Animation Out ketika Out Trigger adalah Manual.

## Catatan beta

- Target awal: Windows 10/11 x64 dan Resolume 7.23.2 atau lebih baru.
- Font harus tersedia pada komputer yang menjalankan Resolume. Jika tidak
  ditemukan, plugin otomatis memakai Arial.
- Grup `Credit` menampilkan identitas tetap `Effect by Belajar Resolume`.
  Perubahan terhadap isinya diabaikan oleh plugin dan credit tidak muncul pada output video.
- Resolume mengatur sendiri tampilan panel parameter, sehingga baris credit
  tidak dapat dipaksa menggunakan warna UI terbalik.
- Versi beta ini perlu diuji dengan beberapa resolusi composition, GPU, font,
  dan panjang teks sebelum didistribusikan sebagai produk final.

## Changelog

### V0.4.1 Beta

- Memindahkan Text Editor ke UI thread khusus dengan message loop Windows sendiri.
- Mencegah editor memblokir render dan antarmuka utama Resolume.
- Klik `Edit Text` kedua kali akan memfokuskan editor yang sudah terbuka,
  bukan membuat beberapa jendela.
- Menambahkan sinkronisasi aman antara editor teks dan thread render OpenGL.
- Editor ditutup dan thread dibersihkan otomatis ketika source atau Resolume ditutup.

### V0.4.0 Beta

- Menambahkan tombol `Edit Text` yang membuka editor multiline khusus.
- Editor dapat diperbesar ke bawah maupun ke samping.
- Tombol Enter membuat baris baru tanpa menutup atau menerapkan editor.
- Menambahkan tombol `Update Text` dan `Cancel`.
- Output baru diperbarui setelah `Update Text` ditekan.
- Mengganti ID internal plugin untuk mencegah cache parameter versi lama.

### V0.3.0 Beta

- Box default memenuhi lebar composition 1920 dan tetap dapat disesuaikan.
- Continuous Repeat menjadi default, dengan separator `|` opsional dan Loop Gap.
- Menambahkan Single Repeat untuk satu teks yang keluar penuh sebelum kembali.
- Menghapus seluruh nomor urut dari nama parameter.
- Mengubah bagian branding menjadi grup `Credit` dengan teks tetap
  `Effect by Belajar Resolume`, tanpa tombol aksi.
- Mengganti ID internal plugin agar parameter versi lama tidak tercampur cache.

### V0.2.1 Beta

- Memindahkan Animation ke bagian paling atas dan memisahkan Start, Pause,
  Resume, End, Loop, serta Bounce.
- Memperbaiki orientasi vertikal tekstur teks GDI+.
- Memperbaiki Flicker agar warna dan alpha benar-benar ikut meredup.
- Menambahkan Box Opacity dan Text Stroke Scale.
- Mendukung kode `\\n` sebagai baris baru pada text input native Resolume.
- Memperbarui ID internal plugin agar layout parameter baru tidak tercampur
  dengan cache/preset versi beta sebelumnya.

### V0.2.0 Beta

- Memperkuat upload tekstur teks GDI+ ke OpenGL agar tidak dipengaruhi state
  pixel-store milik host.
- Menambahkan grup parameter yang dapat dilipat: Text Setting, Box Setting,
  Animation, Effect, dan Animation In Out.
- Menambahkan tombol Start, Pause/Resume, dan End. Source aktif otomatis saat
  ditambahkan.
- Mengubah credit menjadi tombol metadata non-editable.

### V0.1.8 Beta

- Menautkan GLEW secara statis agar plugin dapat dimuat sebagai satu DLL tanpa
  membutuhkan `glew32.dll` terpisah.
- Menambahkan pemeriksaan otomatis export `plugMain` dan dependensi pada
  GitHub Actions sebelum artifact diterbitkan.

### V0.1.7 Beta

- Menghindari konflik nama fungsi `clamp01` dengan FFGL SDK dan membersihkan
  peringatan penyalinan parameter teks.

### V0.1.6 Beta

- Memulihkan deklarasi GDI/metafile yang dibatasi oleh header internal FFGL.

### V0.1.5 Beta

- Menambahkan deklarasi COM `objidl.h`/`ole2.h` dan library `ole32` yang
  dibutuhkan GDI+ pada Windows SDK terbaru.

### V0.1.4 Beta

- Memuat `windows.h` dan `wingdi.h` sebelum FFGL/GDI+ agar seluruh konstanta
  metafile Windows tersedia pada Windows SDK terbaru.

### V0.1.3 Beta

- Memperbaiki kompilasi GDI+: menghapus `WIN32_LEAN_AND_MEAN` agar deklarasi
  metafile GDI yang dibutuhkan oleh GDI+ tetap tersedia.

### V0.1.2 Beta

- Memperbaiki tahap Configure: target GLEW sekarang dibuat pada scope CMake
  proyek utama sebelum target plugin ditautkan.

### V0.1.1 Beta

- Memperbaiki build Windows: include directory GLEW kini diteruskan langsung
  ke target `BRRunningText`.
