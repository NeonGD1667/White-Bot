# Thư viện replay thống nhất: GDR1 / GDR2 / WBR (+ điểm cắm HLR)

Bàn giao gồm thư mục `replay/` (đặt vào `src/replay/` của White Bot, thay cho `src/gdr/`), cùng `CMakeLists.txt`, `tests/`, `oracles/`, `examples/` để kiểm thử độc lập.

```
src/replay/
├── gdr.hpp, gdr.cpp        lõi thư viện (namespace gdr), không phụ thuộc Geode/cocos2d
├── gdr_wb_adapter.hpp      nối Macro hiện tại với model mới
├── json.hpp                bản nlohmann 3.11.3 vendor sẵn của White Bot (giữ nguyên)
├── legacy/                 code GDR1 cũ của v3.1.1: chỉ đổi namespace gdr → wbleg và đường dẫn include
│   ├── legacy_gdr.hpp
│   └── legacy_gdr.cpp
└── slc/                    GIỮ NGUYÊN (không có trong gói này, bạn giữ bản hiện có)
```

## 0. Hai điều khác với đề bài (phát hiện khi đọc source thật)

1. **"GDR cũ" không phải binary có magic `GDR`.** `src/gdr/gdr.hpp` của White Bot chính là **GDR1**: schema JSON, mã hóa bằng MessagePack (`.gdr`) hoặc JSON text. Chỉ **GDR2** mới có magic `GDR` + varint version. Vì vậy hai định dạng này không thể nhầm lẫn: GDR1 bắt đầu bằng `{` hoặc byte map của MessagePack, GDR2 bắt đầu bằng `47 44 52`. Codec "GDR cũ binary" riêng **không tồn tại**; thay vào đó có hai encoding của cùng một schema (`Gdr1MsgPack`, `Gdr1Json`).
2. **README của GDR2 sai so với code** ở vài điểm. Tôi bám theo *code* (thứ mà mọi bot GDR2 thực sự ghi): `duration` là `float` big-endian 4 byte (README ghi varint); string là varint độ dài + bytes, không null-terminated; `bool` là varint; số âm ghi dạng sign-extend 64 bit.

White Bot **đã có** bản GDR2 riêng trong `macro.cpp` (`saveGDR2/importGDR2/exportGDR2`). Nó âm thầm bỏ `FrameFix` và extension, và `std::stoi("v3.1.0-Neon")` cho ra `0`. Thư viện mới sửa các điểm này (xem §5).

## 1. Kiến trúc

```
Replay (model, không biết gì về serialization)
   ▲ decode / encode
ICodec ── WbrCodec        magic 57 42 52 1A, header 24 byte, CRC32, section TLV
       ├─ Gdr2Codec       wire format của reference, + block mở rộng "WBX1" (frame fix...)
       ├─ Gdr1Codec(Json) schema WB, + key cộng thêm (platformer, deaths, rotate)
       ├─ Gdr1Codec(MsgPack)
       └─ (HLR)           registerCodec(std::make_shared<HlrCodec>()) – chưa có spec, KHÔNG được triển khai
```

* **Model** (`gdr::Replay`): metadata, `inputs`, `frameFixes`, `deaths`, `lastRecordedFrame`, extension (`replayExt`, `inputExt`). Phiên bản **bot** (`bot.version`, `bot.versionNumber`) tách khỏi phiên bản **định dạng file** (`source.formatVersion`, `gdr1FormatVersion`).
* **Extension** được giữ nguyên khi định dạng đích chứa được: GDR2 giữ blob nhị phân (kèm input tag); GDR1 giữ các key lạ dưới dạng object MessagePack; WBR giữ cả hai loại. Input extension nằm trong một arena, mỗi `Input` chỉ tốn 16 byte.
* **Lỗi**: `Result<T>` + `ErrorCode` phân biệt *file hỏng* (`Truncated`, `Corrupt`, `ChecksumMismatch`, `InvalidValue`), *không hỗ trợ* (`UnknownFormat`, `UnsupportedVersion`, `UnsupportedFeature`), *bản mới hơn* (`NewerVersion`). Parser không ném exception ra ngoài.
* **Mất dữ liệu**: `LossPolicy::Error` (mặc định) từ chối chuyển đổi nếu đích không chứa được; `ReportAndDrop` vẫn chuyển và ghi từng mục vào `Report`. Mọi thứ bị bỏ qua theo hành vi cũ (rotation, frame fix toàn 0, input không có `frame`) đều có `Diagnostic`.
* **Nhận diện**: `detectFormat()` chỉ dựa vào nội dung (magic/byte đầu), kèm `Confidence`. Khi magic khớp, codec đó là quyết định cuối: lỗi được báo nguyên văn, không thử codec khác (không đoán).

## 2. Bảng tương thích (✔ giữ nguyên, ◐ giữ qua cơ chế mở rộng, ✘ không chứa được → lỗi/`Report`)

| Dữ liệu | GDR1 (JSON/MsgPack) | GDR2 | WBR |
|---|---|---|---|
| author, description, level, bot name | ✔ | ✔ | ✔ |
| bot version là **chuỗi** (`v3.1.0-Neon`) | ✔ | ◐ chuỗi nằm trong `WBX1`; field số = `3001000` | ✔ |
| gameVersion | ✔ (float, `×10000`) | ✔ (int) | ✔ |
| duration / framerate | ✔ (float) | ✔ (float BE / double BE) | ✔ (f32 / f64 bit-exact) |
| seed, coins, ldm | ✔ | ✔ | ✔ |
| platformer | ◐ key `platformer` | ✔ | ✔ |
| button id | ✔ 0..255 | non-platformer: ✘ (chỉ down/up); platformer: 0..3 | ✔ 0..255 |
| thứ tự input P1/P2 khi cùng frame | ✔ | ◐ P1 trước P2 (như reference) | ✔ giữ nguyên |
| `FrameFix` (x, y, rotation, p2) | ✔ | ◐ `WBX1` (chỉ bot `White Bot`/`Dll Bot`) | ✔ |
| cờ `rotate=false` của frame fix | ◐ key `rotate` | ◐ `WBX1` | ✔ |
| `lastRecordedFrame` | ✔ | ◐ `WBX1` | ✔ |
| `deaths` | ◐ key `deaths` | ✔ | ✔ |
| replay extension | JSON key lạ ✔ / blob nhị phân ✘ | blob ✔ / JSON ◐ `WBX1` | ✔ cả hai |
| input extension | JSON key lạ ✔ / blob ✘ | blob ✔ / JSON ◐ tag `WBJ1` | ✔ cả hai |
| checksum | ✘ | ✘ | ✔ CRC-32 |

Các "key cộng thêm" của GDR1 và block `WBX1` của GDR2 là cơ chế mở rộng sẵn có của hai định dạng (reader cũ bỏ qua key lạ / bỏ qua extension theo kích thước). Chúng **không** đổi cấu trúc nhị phân của GDR2. Có thể tắt: `Gdr1Options::writeAdditiveKeys=false`, `Gdr2Options::emitWhiteBotExtension=false` (khi đó phần dữ liệu không chứa được sẽ bị `LossPolicy` xử lý).

## 3. Đặc tả WBR 1.0

Little-endian cho field cố định; float là bit pattern IEEE-754 thô (**không lượng tử hóa**); varint là LEB128 unsigned chuẩn tắc (tối đa 10 byte, từ chối dạng overlong); số có dấu dùng zigzag.

```
 0  4  magic 57 42 52 1A ("WBR" + Ctrl-Z)
 4  1  major = 1        reader: >1 → NewerVersion, 0 → UnsupportedVersion
 5  1  minor = 0        mọi minor của major 1 đều đọc được
 6  1  compression id   0 = không nén; id khác → UnsupportedFeature
 7  1  reserved = 0
 8  4  flags            16 bit thấp: thông tin (1 fixes, 2 deaths, 4 input ext, 8 replay ext)
                        16 bit cao: tính năng BẮT BUỘC; bit lạ → UnsupportedFeature
12  4  rawSize          16  4 storedSize (== rawSize khi chưa nén)      20  4 CRC-32 của payload
24  .. payload = chuỗi section: u8 id, varint length, bytes
      id < 0x80 : critical (không biết → UnsupportedFeature)   id ≥ 0x80 : optional (không biết → bỏ qua)
```

| id | Section | Nội dung |
|---|---|---|
| 0x01 | META | bits{ldm, platformer, hasBotVersionNumber, hasLastRecordedFrame}, f32 duration, f64 framerate, svarint gameVersion/seed/coins, varint level.id, 5 string, [svarint botVersionNumber], [varint lastRecordedFrame] |
| 0x02 | INPUTS | varint count, u8 flags{buttonMode(0 ngầm định =1 / 1 hai bit / 2 varint tường minh), signedDeltas, hasP2}; mỗi input một varint `(Δframe << s) \| [button] \| [p2<<1] \| down` |
| 0x03 | FRAMEFIXES | count, flags{signedDeltas, hasP2, hasNoRotate}, cột Δframe, [bitmap rotate], 3 hoặc 6 cột float |
| 0x04 | DEATHS | count, flags, Δ varint |
| 0x81 | REPLAY_EXT | kind, tag, length, bytes |
| 0x82 | INPUT_EXT | kind, tag, số input có ext, (Δindex, length, bytes)… |

* **Delta + varint** cho frame; input một stream chronological duy nhất (giữ đúng thứ tự P1/P2 cùng frame), bit `p2` chỉ tồn tại khi có P2, bit button chỉ tồn tại khi cần. Nếu input không tăng dần, writer tự chuyển sang zigzag delta (vẫn lossless).
* **Float column**: với mỗi cột, XOR với giá trị trước rồi bỏ các byte đầu bằng 0 (nibble điều khiển + byte còn lại). Hoàn toàn lossless, kể cả `-0.0` và NaN payload (có test).
* **Giới hạn**: payload < 4 GiB; `Limits` mặc định: 1 GiB file, 2²⁶ input/fix, 1 MiB mỗi string, 64 MiB extension. Mọi `count` bị chặn bởi số byte còn lại *trước khi* cấp phát.
* **Nén**: header đã dành `compression id`, nhưng bản này **chưa triển khai nén** (xem §6).

## 4. Ví dụ: GDR2 cũ → WBR (`examples/gdr2_to_wbr.cpp`)

```cpp
gdr::Report report;
gdr::ImportOptions in;  in.report = &report;                 // Format::Auto: nhận diện theo nội dung
auto replay = gdr::importReplayFile("old.gdr2", in);
if (!replay) { log(replay.error().toString()); return; }

gdr::ExportOptions out; out.format = gdr::Format::Wbr;       // LossPolicy::Error mặc định
out.report = &report;
auto saved = gdr::exportReplayFile(replay.value(), "new.wbr", out);
```
Chạy thật trên file do reference tạo (2000 input, platformer): `3451 B (GDR2) → 3480 B (WBR)`; WBR → WBR ổn định từng byte.

## 5. Hành vi của hai source gốc mà thư viện giữ lại / sửa

**Giữ nguyên (bug-for-bug, có thể đổi bằng option):**
* Frame offset DLL Bot: `+1` cho input và frame fix của macro `Dll Bot` cũ hơn 2.3.6. Cổng nguyên văn logic WB, kể cả cách `numFromString` của Geode (`"6-beta"` → 6) và việc version có > 3 phần (vd. `v3.0.0-beta.2`) vẫn bị `+1`. `ImportOptions::applyLegacyDllBotFrameOffset`.
* Rotation của frame fix: WB chỉ đọc `r` với macro `Dll Bot` không chứa `alpha.`/`beta.` (ép `true` cho `v3.0.1`). Mọi macro khác (**kể cả file do chính White Bot ghi**) bị bỏ rotation. Đây trông giống bug nhưng tôi giữ làm mặc định theo yêu cầu; mỗi lần bỏ đều có `gdr1.rotation-dropped`. `RotationPolicy::PreserveAll` giữ lại tất cả.
* Bốn cách mã hóa frame fix cũ (`player1` chuỗi, `player1X/Y`, `p1/p2`) và việc bỏ qua input không có `frame`.

**Sửa (khác với code cũ, có test):**
* Ghi lại macro DLL Bot cũ: code cũ cộng `+1` mỗi lần lưu lại (drift). Writer mới trừ đúng phần reader sẽ cộng, nên `đọc → ghi → đọc` là phép đồng nhất.
* GDR2 của WB: không còn mất `FrameFix`, extension, `lastRecordedFrame`; chuỗi version không còn thành `0`.
* GDR1: frame fix có `p2` nhưng `p2.y == 0` không còn bị bỏ; cờ `rotate=false` không còn bị đổi thành `true`.
* Parser không crash/UB với file hỏng: code cũ dùng `operator[]` trên JSON const thiếu key, và `get<int>` ném exception không bắt.
* Reference GDR2 **không đọc nổi file do chính nó ghi** nếu `seed` âm (varint 10 byte, `Invalid extension size`). Reader mới đọc được; writer mới vẫn ghi sign-extend như reference/WB để byte-compatible.
* Reader GDR2 mới kiểm tra `version == 2` (reference nhận mọi giá trị) và có giới hạn kích thước/đếm.
* `importGDR2` hiện tại của WB không sắp xếp input theo frame (P1 hết rồi mới tới P2); reader mới sắp xếp ổn định, P1 trước P2 khi trùng frame (giống reference). *Tôi chưa kiểm tra xem nơi gọi của WB có tự sắp xếp hay không.*

## 6. Kết quả thực tế

Môi trường: Linux, g++ 13.3, `-std=c++23`, nlohmann 3.11.3 (đúng bản WB đang vendor).

| Kiểm thử | Kết quả |
|---|---|
| `gdr_tests` (17 test, 280 check; ASan + UBSan) | **đạt**: round-trip 4 định dạng, mọi cặp chuyển đổi, FrameFix/extension, offset DLL Bot, rotation, file rỗng/cắt cụt (mọi prefix của 4 định dạng)/sai header/CRC/version mới hơn, count & extension size sai, thứ tự P1/P2, loss policy, HLR stub, fuzz 16 000 biến đổi + 3 000 file ngẫu nhiên không crash |
| `gdr_diff_tests` (671 check) | **đạt**: so với **code gốc** chạy như oracle. (A) 60 cấu hình GDR1 (JSON+MsgPack, DLL Bot nhiều version, 0/1/250 input): reader mới == importer cũ của WB trên file do writer mới *và* writer cũ tạo; (B) 32 cấu hình GDR2: đọc đúng file của reference, **ghi trùng từng byte** với reference, reference đọc lại được |
| `gdr_adapter_check` | **đạt**: model cũ thật (`replay/legacy`, `wbleg::Replay`) → adapter → model mới giống hệt import trực tiếp; vòng ngược lại cho `exportData` cũ cùng bytes |
| `cmake` + `ctest` (Release) | **đạt** |

**Benchmark, dữ liệu TỔNG HỢP** (không có macro thật; track vật lý giả lập, 1/6 input là P2; best-of-N, 1 core). Cột "GDR2" không chứa frame fix (reader ngoài không lưu được), "GDR2+WBX1" chứa:

| inputs / fixes | GDR2 | GDR2+WBX1 | **WBR** | MsgPack | JSON |
|---|---|---|---|---|---|
| 5 000 / 3 000 | 5.8 KB | 53.2 KB | **54.0 KB** | 309 KB | 596 KB |
| 100 000 / 60 000 | 114 KB | 959 KB | **976 KB** | 6.37 MB | 11.7 MB |
| 1 000 000 / 600 000 | 1.15 MB | 8.89 MB | **9.05 MB** | 64.2 MB | 116 MB |

| 1M / 600k | encode | decode | RAM thêm |
|---|---|---|---|
| WBR | 66 ms | 67 ms | ~27 MB |
| GDR2+WBX1 | 65 ms | 89 ms | ~28 MB |
| MsgPack | 2181 ms | 3873 ms | ~2.2 GB |
| JSON | 2315 ms | 3109 ms | ~1.4 GB |

Kết luận trung thực: so với GDR1, WBR nhỏ hơn **~7× (MsgPack) / ~13× (JSON)** và nhanh hơn 30–60×. Nhưng so với **GDR2+WBX1 thì WBR không nhỏ hơn** (lớn hơn ~1–2% vì header 24 byte, section, và 1 bit `p2` cho mỗi input khi có P2), vì cả hai dùng cùng bộ mã hóa cột. Giá trị của WBR là: một stream giữ đúng thứ tự, CRC-32, version/feature flag/section mở rộng được, lưu được mọi thứ mà không cần mượn extension của định dạng khác.

**Nén:** giới hạn trên đo bằng zlib-9/lzma trên chính payload WBR cho thấy còn ~59–65% (zlib/lzma) với frame fix tổng hợp, nhưng generator của tôi lặp lại đều hơn macro thật nên con số này **không đủ cơ sở** để cam kết. Vì vậy chưa nhúng bộ nén. Nếu bạn gửi vài macro thật, tôi đo lại; ứng viên gọn nhất là DEFLATE qua `miniz` (1 file, không cần link thêm) với `compression id = 1`, hoặc `zstd ≥ 1.5` nếu cần tỉ lệ cao hơn.

## 7. Tích hợp vào White Bot

1. **Đổi tên thư mục** `src/gdr/` → `src/replay/`. Giữ nguyên `slc/` và `json.hpp`. Thay `gdr.hpp`/`gdr.cpp` cũ bằng thư mục `replay/` trong gói (gồm `gdr.hpp`, `gdr.cpp`, `gdr_wb_adapter.hpp`, `legacy/`). `slc/` không include `gdr.hpp`, `json.hpp` hay `gdr::` nên không phải sửa file nào trong đó. Dependency: **không có gì thêm**, `gdr.cpp` dùng chính `replay/json.hpp`.
2. **Sửa đường dẫn include và đổi `gdr::` → `wbleg::`** (đã đếm toàn bộ chỗ dùng trong v3.1.1):

| File | Sửa |
|---|---|
| `macro.hpp`, `macro.cpp` | **thay nguyên file** bằng `macro/macro.hpp`, `macro/macro.cpp` |
| `hacks/show_trajectory.cpp` | 6 chỗ `gdr::FrameData` / `gdr::FrameFix` → `wbleg::` |
| `global.cpp` dòng 321, `main.cpp` dòng 605 | `gdr::FrameFix` → `wbleg::FrameFix` |
| `CMakeLists.txt` dòng 32 | `src/gdr/gdr.cpp` → `src/replay/gdr.cpp` và `src/replay/legacy/legacy_gdr.cpp` (+ đổi đường dẫn nếu `slc` có file `.cpp`) |

   Đây chỉ là đổi tên: `Macro` và hàng trăm chỗ dùng `macro.inputs` / `macro.frameFixes` giữ nguyên.
3. **Thay `macro.hpp` và `macro.cpp`** bằng hai file trong `macro/` (đã viết sẵn, xem §10). Không cần tự sửa thân hàm nào nữa.
4. **Quy tắc đã đổi cần biết**: lưu GDR2 giờ có block `WBX1` (vài chục byte) với bot `White Bot`; lưu `.gdr`/`.gdr2` dùng `LossPolicy::ReportAndDrop` (không chặn việc lưu, mọi thứ bị bỏ được ghi vào log Geode).
5. **HLR**: khi có spec, viết `class HlrCodec : public gdr::ICodec` rồi `gdr::registerCodec(...)`; `Replay` và các codec khác không phải sửa (đã có test với codec giả).

## 8. Chưa xác minh / giới hạn

* **Chưa build với Geode SDK** (Windows/Android/macOS, MSVC/clang). Lõi không dùng Geode/cocos nên chỉ cần C++20 + nlohmann; `gdr_wb_adapter.hpp` đã biên dịch với model cũ thật (cocos/Geode được stub), **chưa** với `Macro` thật. Chỉ chạy trên Linux/g++ 13; chưa thử clang/MSVC.
* **Chưa có macro thật** từ DLL Bot / xdBot / White Bot: tương thích được chứng minh bằng việc so sánh với *code* gốc trên dữ liệu tạo ra, không phải bằng file ngoài đời. Đặc biệt quirk offset cho version có hậu tố (`v3.0.0-beta.2` → `+1`) là port nguyên văn, chưa đối chiếu với macro DLL Bot thật.
* **Số liệu benchmark là tổng hợp**; tỉ lệ thực tế phụ thuộc cách macro thật phân bố frame/float.
* **Chưa triển khai**: nén WBR, codec HLR (chưa có spec), SLC2/SLC3/XD của White Bot (ngoài phạm vi đề bài, có thể thêm như codec riêng).
* Mặc định `RotationPolicy::LegacyWhiteBot` làm mất rotation của macro `White Bot` khi đọc (hành vi cũ); nên xem lại có chủ đích không.
* `Replay::seed` là `int32` (bit pattern của seed 32 bit); `Macro::seed` kiểu `uintptr_t` cần cast như ví dụ trên.
* Định dạng GDR1 ghi số `float` giống WB; macro có `framerate` không biểu diễn được bằng float sẽ được ghi dạng `double` (reader cũ đọc vẫn được).

## 9. Chạy lại kiểm thử

```sh
# cần checkout của reference GDR2 (nhánh gdr2) và White Bot v3.1.1 chỉ cho differential test
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGDR2_REFERENCE_DIR=<GDReplayFormat> -DWHITEBOT_DIR=<White-Bot>
cmake --build build && (cd build && ctest --output-on-failure)     # gdr_tests, gdr_adapter_check, gdr_diff_tests
# sanitizer:  g++ -std=c++23 -O1 -g -fsanitize=address,undefined -Ireplay replay/gdr.cpp tests/gdr_tests.cpp
# benchmark:  build/gdr_bench <inputs> <fixes> {gdr2|gdr2wbx|json|msgpack|wbr}
```

## 10. `macro.hpp` / `macro.cpp` viết lại (thư mục `macro/`)

**Quan hệ giữa các lớp (đọc từ source v3.1.1):** `Macro` kế thừa `wbleg::Replay<Macro, input>` (model GDR1 cũ, CRTP; `input : wbleg::Input`). Model mới `gdr::Replay` là struct thường, không phải template, nên **không thể** thay thế chỗ kế thừa mà không viết lại hàng trăm chỗ dùng `macro.inputs` / `macro.frameFixes`. Vì vậy `Macro` giữ model cũ (đổi tên namespace) và chỉ phần đọc/ghi file chuyển sang thư viện mới qua `gdr::wb::toReplay/fromReplay`. SLC2/SLC3 và XD không đổi.

**`WhiteBotVersion`** vẫn là `const std::string` toàn cục ở `legacy_gdr.hpp` (đúng như cũ: `"v3.1.0-Neon"`); `macro.hpp` include file đó nên `Macro()`, `macro.cpp:390` và `ui/record_layer.cpp:1085` dùng được nguyên xi. Lưu ý: giá trị này vẫn là `v3.1.0-Neon` dù repo là v3.1.1, tôi không đổi.

**API công khai giữ nguyên chữ ký:** `Macro::importData(data, importInputs=true)`, `exportData(json=false)`, `isGDR2Data`, `importGDR2`, `exportGDR2`, `save`, `saveGDR2`, mọi hàm SLC/XD, `ui/load_macro_layer.cpp` và `ui/macro_cell.cpp` không phải sửa. `importData`/`exportData` giờ là hàm của `Macro` (che hàm cùng tên của lớp cha) và `importData` tự nhận GDR1/GDR2/WBR theo nội dung. Thêm mới: `tryImport()` (trả lý do lỗi), `exportWBR()`, `saveWBR()` (chưa nối vào UI), mã lỗi lưu `22` = serialize thất bại.

**Khác với v3.1.1 (cố ý, đã kiểm tra):**
* `exportGDR2` cũ ghi `gameVersion = static_cast<int>(2.2081f) = 2`; giờ ghi `22081`. Macro chuyển từ `Macro::gameVersion` theo cả hai quy ước (`2.2081` khi ghi âm, `22081` khi nhập từ XD/GDR2).
* Seed: `importData` cũ gán vào `Macro::seed` (do member che), còn `exportData` ghi `seed` của lớp cha (luôn 0) nên **seed bị mất mỗi lần lưu lại**. Giờ cả hai được đặt và seed được giữ.
* GDR2 lưu thêm `FrameFix`, `lastRecordedFrame`, chuỗi version trong block `WBX1` (cũ: bỏ hết, version `v3.1.0-Neon` thành `0`).
* Lưu lại macro `Dll Bot` cũ không còn dịch thêm +1 frame mỗi lần.
* File hỏng như `{"inputs":[{"frame":1}]}` làm bản cũ crash; bản mới trả `Macro` rỗng (giống hành vi "file không đọc được" cũ) và ghi lỗi vào log.
* `isGDR2Data` dùng `gdr::detectFormat` (vẫn chỉ xét magic `GDR`).

**Kiểm chứng (tests/macro_harness):** build hai harness từ *văn bản thật* của `macro.hpp` và khối GDR-IO của `macro.cpp` (cũ v3.1.1 và mới), stub Geode/cocos2d, rồi so sánh hành vi. Kết quả: 94 so sánh, 0 lỗi, gồm: byte ghi ra `.gdr`/`.gdr.json` **giống hệt** bản cũ (trừ khóa `platformer` bổ sung và macro Dll Bot cũ), hai bên đọc được file của nhau cho GDR1 và GDR2, import→export giống hệt, macro WBR qua `Macro` đọc lại đúng. **Chưa thể biên dịch toàn bộ `macro.cpp` vì thiếu Geode SDK**: phần ngoài khối GDR-IO chỉ có thay đổi đường dẫn include, một cast `wbleg::`, và hai dòng `return 22`.
