# BR Running Text — V0.1 Beta

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
   `BR-Running-Text-Windows-v0.1-beta`.

Tidak perlu menginstal Visual Studio di komputer Anda. GitHub Actions akan
mengambil FFGL SDK resmi dan membangun DLL Windows x64.

## Instalasi

1. Tutup Resolume.
2. Salin `BR Running Text.dll` ke folder plugin FFGL Resolume Anda.
3. Buka Resolume kembali.
4. Cari **BR Running Text** pada daftar Sources.
5. Tarik source ke sebuah clip pada layer tersendiri.

Lokasi plugin yang umum:

`Documents\\Resolume Arena\\Extra Effects`

Jika plugin belum terlihat, periksa lokasi plugin pada Preferences Resolume.

## Cara penggunaan

- Tanpa Enter: teks dianggap sebagai running text satu baris.
- Menggunakan Enter: teks menjadi multiline untuk mode credit Up/Down.
- Motion `Static`: digunakan sebagai lower third.
- Animation In/Out dalam keadaan Off secara default.
- `Reset Position` mengulang waktu motion dan animasi.
- `Play Out` menjalankan Animation Out ketika Out Trigger adalah Manual.

## Catatan beta

- Target awal: Windows 10/11 x64 dan Resolume 7.23.2 atau lebih baru.
- Font harus tersedia pada komputer yang menjalankan Resolume. Jika tidak
  ditemukan, plugin otomatis memakai Arial.
- Credit `Effect by Belajar Resolume` tersimpan permanen di metadata dan baris
  parameter terakhir; credit tidak muncul pada output video.
- Resolume mengatur sendiri tampilan panel parameter, sehingga baris credit
  tidak dapat dipaksa menggunakan warna UI terbalik.
- Versi beta ini perlu diuji dengan beberapa resolusi composition, GPU, font,
  dan panjang teks sebelum didistribusikan sebagai produk final.
