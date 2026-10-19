# README_MauIC_DoSau

Tai lieu nay tom tat phan code quan ly mau IC va tong do sau phay trong firmware man hinh CNC. Muc tieu la lan sau mo lai du an, nguoi sua code hoac Codex co the doc nhanh file nay de biet can sua o dau.

## 1. File code chinh

Phan mau IC nam chu yeu trong:

```text
TFT/src/User/Menu/Terminal.c
```

Man hinh van hanh phay IC la ham:

```c
void menuGcodeKeyboard(void)
```

## 2. File du lieu mau IC tren the SD

Firmware doc/ghi mau IC tu file:

```text
ic_depth.cfg
```

Trong cay phat hanh SD da co san:

```text
FileSD/ic_depth.cfg
FileSD/V1/ic_depth.cfg
```

Dinh dang moi dong:

```text
Folder/TenMau=tong_do_sau_mm
```

Vi du:

```text
Seri 7-8/IP7_CPU=0.71
Seri 14/IP14_CPU=0.75
Seri X/IPX_CPU=0.72
```

Neu khong co folder, mau se nam o thu muc goc:

```text
IP14_CPU_TEST=0.75
```

## 3. Du lieu goc da rut gon

Du lieu goc nam o:

```text
C:\Users\Tong Van Hao\Downloads\DATA_all\DATA
```

Quy tac rut gon:

- Moi thu muc con la mot folder trong man hinh `Mau IC`, vi du `Seri 14`.
- Moi file txt la mot mau IC.
- Lay dong so cuoi cung trong file lam `Tong do sau`.
- Bo qua cac file co dong cuoi bang `0`, vi code chi chap nhan do sau tu `0.01` den `1.00`.

Vi du file:

```text
C:\Users\Tong Van Hao\Downloads\DATA_all\DATA\Seri 7-8\IP7_CPU.txt
```

Dong cuoi hop le la:

```text
0.71
```

Nen ghi vao `ic_depth.cfg`:

```text
Seri 7-8/IP7_CPU=0.71
```

## 4. Cac gioi han quan trong

Trong `Terminal.c`:

```c
#define IC_TEMPLATE_MAX_COUNT 256
#define IC_TEMPLATE_FOLDER_LEN 16
#define IC_TEMPLATE_NAME_LEN 32
#define IC_TEMPLATE_PAGE_SIZE 5
```

Y nghia:

- Toi da 256 mau IC.
- Ten folder toi da 15 ky tu + null.
- Ten mau toi da 31 ky tu + null.
- Moi trang danh sach hien 5 dong mau/folder, dong duoi la `Truoc / Sau / Back`.

Sau khi tang `IC_TEMPLATE_MAX_COUNT` len 256, build bao:

```text
RAM:   46.5%
Flash: 95.7%
```

## 5. Cau truc du lieu trong code

Struct mau IC:

```c
typedef struct
{
  char folder[IC_TEMPLATE_FOLDER_LEN];
  char name[IC_TEMPLATE_NAME_LEN];
  float totalDepth;
} IC_DEPTH_TEMPLATE;
```

Mang luu mau:

```c
static IC_DEPTH_TEMPLATE icTemplates[IC_TEMPLATE_MAX_COUNT];
static uint8_t icTemplateCount = 0;
static char icTemplateCurrentFolder[IC_TEMPLATE_FOLDER_LEN] = {'\0'};
static int8_t selectedIcTemplateIndex = -1;
```

## 6. Cac ham quan trong

Doc file `ic_depth.cfg`:

```c
static void icTemplateLoadFile(void)
```

Ghi lai file `ic_depth.cfg`:

```c
static bool icTemplateWriteFile(void)
```

Luu mau hien tai theo ten nguoi dung nhap:

```c
static bool icTemplateSaveCurrentDepth(const char * name)
```

Xoa mau:

```c
static bool icTemplateDelete(uint8_t index)
```

Tach duong dan `Folder/TenMau`:

```c
static bool icTemplateParsePath(...)
```

Danh sach folder/mau:

```c
static bool icTemplateListPopup(void)
```

Menu thao tac khi bam vao mau:

```c
static bool icTemplateActionPopup(uint8_t templateIndex)
```

Popup nhap ten mau:

```c
static bool icTemplateNameInputPopup(void)
```

## 7. Luong giao dien hien tai

Trong man hinh phay IC:

- Nut `Mẫu IC`: mo danh sach folder/mau.
- Nut `Tổng độ sâu`: chi hien/nhac thong tin tong do sau.
- Nut giua hang `- / 0.75 / +`: hien gia tri tong do sau hien tai.
- Nut `Lưu`: luu tong do sau hien tai thanh mau moi.
- Nut `Đèn`: bat/tat den.

Trong `Mẫu IC`:

- O root hien folder dang `[Seri 14]`, `[Seri 7-8]`, ...
- Bam folder se mo danh sach mau ben trong.
- Bam `Back` trong folder se quay ve root.
- Bam `Back` o root se quay lai man hinh phay IC.
- Bam mau se mo menu:

```text
Chọn
Sửa độ sâu
Xóa mẫu
Back
```

## 8. Chon/sua/xoa mau

Khi bam `Chọn`, code:

```c
selectedIcTemplateIndex = templateIndex;
setMillingTotalDepth(icTemplates[templateIndex].totalDepth);
```

Khi bam `Sửa độ sâu`, code dung:

```c
editFloatValue(MIN_MILLING_TOTAL_DEPTH, 1.0f, ...)
```

Sau do ghi lai `ic_depth.cfg`.

Khi bam `Xóa mẫu`, code xoa phan tu trong `icTemplates[]`, dich mang len, roi ghi lai `ic_depth.cfg`.

## 9. Luu mau moi

Khi bam nut `Lưu`, popup ban phim mo ra. Nguoi dung co the nhap:

```text
Seri 14/IP14_CPU_TEST
```

Neu co dau `/`, phan truoc la folder, phan sau la ten mau.

Neu khong co dau `/`, mau se duoc luu vao folder hien tai neu dang o trong folder; neu dang o root thi luu o root.

Sau khi luu, code ghi:

```text
Seri 14/IP14_CPU_TEST=0.75
```

## 10. Luu y khi sua tiep

- Khong nen tang `IC_TEMPLATE_MAX_COUNT` qua cao neu chua kiem tra RAM.
- Neu them truong moi vao mau, can doi format file va ham parse/write.
- Neu muon chap nhan do sau `0`, can sua dieu kien `depth < MIN_MILLING_TOTAL_DEPTH`.
- Neu ten folder dai hon 15 ky tu, code se cat theo `IC_TEMPLATE_FOLDER_LEN`.
- Neu ten mau dai hon 31 ky tu, code se cat theo `IC_TEMPLATE_NAME_LEN`.
- File `ic_depth.cfg` nen de ASCII/UTF-8 khong BOM va han che ky tu dac biet trong ten mau.

## 11. Lenh build kiem tra

Sau khi sua code:

```powershell
platformio run
```

Firmware build ra:

```text
.pio/build/BIGTREE_TFT35_V3_0/BIGTREE_TFT35_V3.0.28.x.bin
```

Can kiem tra:

- Build thanh cong.
- `Mẫu IC` mo duoc root folder.
- Vao folder, Back ve root duoc.
- Back o root ve man hinh phay IC duoc.
- Chon mau cap nhat dung `Tổng độ sâu`.
- Sua do sau ghi lai file.
- Xoa mau ghi lai file.
- Luu mau moi co dung folder/name.

## 12. Quy trinh van hanh may phay IC cho nguoi dung

Muc tieu cua quy trinh nay la giup tho sua dien thoai phay IC nhanh, lap lai duoc, va giam sai so khi lam nhieu dong iPhone khac nhau.

### 12.1. Chuan bi truoc khi phay

1. Bat may CNC va man hinh dieu khien.
2. Gan dao phay dung loai can dung cho IC.
3. Co dinh main dien thoai len ban may.
4. Kiem tra SD da co file cau hinh `ic_depth.cfg`.
5. Vao man hinh `Phay IC`.
6. Bam `Home` de may ve goc an toan.
7. Kiem tra den, truc Z, dau dao va vung phay.

### 12.2. Chon mau IC co san

1. Bam `Mẫu IC`.
2. Chon folder theo dong may, vi du `Seri 14`.
3. Chon dung con IC can phay, vi du `IP14_CPU`.
4. Bam `Chọn`.
5. Man hinh tu dong nap `Tổng độ sâu` cua mau da chon.

Vi du:

```text
Seri 14/IP14_CPU=0.45
```

Khi chon mau nay, may se tu cap nhat `Tổng độ sâu = 0.45 mm`.

### 12.3. Test do sau va luu mau moi

Dung cho truong hop gap dong may moi, IC moi, main da qua sua chua, hoac can tinh chinh do sau.

1. Chinh `Tổng độ sâu` bang nut `- / +`.
2. Phay thu voi do sau nho truoc.
3. Quan sat ket qua phay thuc te.
4. Neu chua toi lop can phay, tang do sau tung buoc nho.
5. Khi da co do sau hop ly, bam `Lưu`.
6. Dat ten mau theo dang:

```text
Seri 14/IP14_CPU_TEST
```

Hoac neu dang o trong folder `Seri 14`, chi can nhap:

```text
IP14_CPU_TEST
```

Lan sau chi can vao `Mẫu IC`, chon lai mau da luu, may se tu nap lai `Tổng độ sâu`.

### 12.4. Sua hoac xoa mau

Trong `Mẫu IC`, khi chon mot mau da luu, man hinh co cac muc:

```text
Chọn
Sửa độ sâu
Xóa mẫu
Back
```

- `Chọn`: nap do sau cua mau vao man hinh phay IC.
- `Sửa độ sâu`: sua lai do sau neu sau khi dung thuc te thay can tinh chinh.
- `Xóa mẫu`: xoa mau khong dung nua.
- `Back`: quay lai danh sach mau/folder.

Luu y: nen dat ten mau ngan gon, de nho, khong trung nhau. Vi du:

```text
Seri 7-8/IP7_CPU
Seri X/IPX_CPU
Seri 11/IP11_BB
Seri 14/IP14_CPU
```

### 12.5. Mo file SD

Trong man hinh phay IC, nut `Tệp` da duoc chinh de vao thang danh sach file tren SD cua man hinh TFT.

Quy trinh moi:

```text
Phay IC -> Tệp -> Danh sach file SD
```

Nguoi dung khong can qua man hinh chon nguon file nua.

### 12.6. Quy trinh phay de quay video huong dan

Co the dung quy trinh ngan gon sau de quay video:

1. Gioi thieu man hinh `Phay IC`.
2. Bam `Home`.
3. Bam `Mẫu IC`.
4. Chon folder, vi du `Seri 14`.
5. Chon `IP14_CPU`.
6. Cho khach thay `Tổng độ sâu` tu dong thay doi.
7. Dat vi tri/vung phay tren main.
8. Tao file phay.
9. Bam chay phay.
10. Sau khi phay xong, cho xem ket qua tren IC.
11. Neu test ra do sau moi, chinh lai `Tổng độ sâu`, bam `Lưu`, dat ten mau moi.
12. Mo lai `Mẫu IC` de chung minh mau vua luu da co trong danh sach.

## 13. Diem thong minh cua giai phap

Day khong chi la may CNC chay file G-code thuong, ma da co logic rieng cho cong viec phay IC dien thoai.

### 13.1. Co thu vien do sau theo tung IC

Moi dong iPhone va tung loai IC co the co do sau khac nhau. Firmware cho phep luu thanh mau:

```text
Folder/TenMau=Tổng độ sâu
```

Vi du:

```text
Seri 14/IP14_CPU=0.45
Seri 13/IP13_CPU=0.50
Seri X/IPX_CPU=0.72
```

Nguoi dung khong can nho bang tay, khong can nhap lai moi lan.

### 13.2. Co the tu hoc tu kinh nghiem thuc te

Khi tho phay test va tim duoc do sau tot, co the bam `Lưu` de bien ket qua thuc te thanh mau dung lai ve sau.

Gia tri ban dau co the lay tu tai lieu, nhung gia tri sau cung co the duoc toi uu theo kinh nghiem cua tung xuong.

### 13.3. Giam thao tac sai

Chon dung mau IC se tu dong nap `Tổng độ sâu`, giam rui ro:

- Nho nham do sau.
- Nhap sai so.
- Phay qua sau lam hong main.
- Phay qua nong khong sach keo/lop IC.

### 13.4. Danh sach chia theo folder

Du lieu duoc chia theo folder nhu:

```text
Seri 7-8
Seri X
Seri 11
Seri 12
Seri 13
Seri 14
```

Voi cach nay, khi so luong mau tang len, nguoi dung van de tim.

### 13.5. Nut Tep vao SD truc tiep

Trong thuc te van hanh, nguoi dung thuong chi can mo file tren SD. Vi vay nut `Tệp` vao thang SD se nhanh hon, it buoc hon, phu hop hon khi dung may hang ngay.

## 14. Kich ban video huong dan su dung

### 14.1. Video ngan 60-90 giay

Muc tieu: quang ba nhanh, cho khach thay may de dung va co thu vien IC.

Canh 1 - Gioi thieu nhanh:

```text
Day la may phay IC dien thoai co man hinh dieu khien rieng cho tho sua iPhone.
```

Canh 2 - Chon mau:

```text
Chi can vao Mẫu IC, chon dung dong may, vi du Seri 14, roi chon IP14_CPU.
```

Canh 3 - Tu nap do sau:

```text
May tu dong nap Tong do sau da luu san, khong can nho thong so bang tay.
```

Canh 4 - Chay phay:

```text
Dat vung phay, tao file, may se phay theo thong so da chon.
```

Canh 5 - Luu kinh nghiem:

```text
Neu test ra do sau tot hon, bam Luu de lan sau dung lai ngay.
```

Canh 6 - Ket thuc:

```text
Giai phap giup tho phay IC nhanh hon, deu hon, va giam rui ro khi lam nhieu dong iPhone.
```

### 14.2. Video huong dan chi tiet 3-5 phut

Muc tieu: day nguoi mua dung may sau khi nhan hang.

Bo cuc de quay:

1. Gioi thieu cac nut tren man hinh `Phay IC`.
2. Giai thich `Mẫu IC`, `Tổng độ sâu`, `Lưu`, `Tệp`, `Đèn`.
3. Huong dan chon mau co san.
4. Huong dan chinh `Tổng độ sâu`.
5. Huong dan luu mau moi.
6. Huong dan sua/xoa mau.
7. Huong dan mo file SD bang nut `Tệp`.
8. Thuc hien mot lan phay demo tren main.
9. Cho xem ket qua sau khi phay.
10. Nhac lai cac luu y an toan: can Home, can set dung vung phay, tang do sau tung buoc khi test.

Loi thoai goi y:

```text
Truoc day moi lan phay IC, tho phai nho do sau hoac ghi ra giay. Neu nhap sai rat de phay qua sau.

Voi phien ban nay, moi dong IC co the luu thanh mot mau rieng. Vi du CPU iPhone 14 co Tong do sau 0.45 mm. Lan sau chi can chon IP14_CPU, may tu dong nap lai thong so.

Khi gap main moi hoac can test, ta co the tang giam Tong do sau truc tiep. Neu da ra ket qua dep, bam Luu, dat ten mau. Tu lan sau khong can test lai tu dau.
```

### 14.3. Video ban hang 2-3 phut

Muc tieu: thuyet phuc khach mua may bang loi ich thuc te.

Thong diep chinh:

```text
May phay IC nay khong chi chay CNC, ma duoc toi uu rieng cho tho sua dien thoai.
```

Y chinh nen noi:

- Co thu vien do sau theo dong iPhone.
- Co the tu luu mau sau khi test.
- Giam phu thuoc vao tay nghe va ghi chep thu cong.
- Giam rui ro phay sai do sau.
- Thao tac tren man hinh truc tiep, khong can may tinh.
- Nut `Tệp` vao thang SD, mo file nhanh.
- Phu hop cho cua hang sua chua lam nhieu main moi ngay.

Loi thoai goi y:

```text
Diem manh cua may la kha nang luu mau IC. Khi anh em da co thong so dep cho mot dong may, chi can luu lai. Lan sau gap cung IC do, chon mau la may tu nap Tong do sau.

Voi cua hang lam nhieu may, viec nay giup thao tac nhanh hon, dong deu hon, va dao tao tho moi cung de hon.
```

Canh nen quay:

- Canh can man hinh khi chon `Mẫu IC`.
- Canh danh sach folder theo Seri iPhone.
- Canh `Tổng độ sâu` tu thay doi sau khi chon mau.
- Canh bam `Lưu` de tao mau moi.
- Canh dau dao phay tren main.
- Canh ket qua IC sau khi phay.

## 15. Loi ich ban hang nen nhan manh

Khi gioi thieu voi khach, nen tap trung vao loi ich, khong chi noi ve tinh nang.

Tinh nang:

```text
Luu duoc mau IC va Tong do sau.
```

Loi ich:

```text
Nguoi dung khong can nho thong so, giam nhap sai, lam lai nhanh hon.
```

Tinh nang:

```text
Co folder theo Seri iPhone.
```

Loi ich:

```text
Danh sach nhieu mau van de tim, phu hop cho tiem sua nhieu dong may.
```

Tinh nang:

```text
Co nut Luu sau khi test.
```

Loi ich:

```text
Kinh nghiem thuc te cua tho duoc luu lai thanh du lieu dung lau dai.
```

Tinh nang:

```text
Nut Tep vao SD truc tiep.
```

Loi ich:

```text
Rut ngan thao tac, dung may nhanh hon trong cong viec hang ngay.
```

## 16. Cac diem nen cai tien tiep neu lam ban thuong mai

De san pham chuyen nghiep hon khi ban ra thi nen can nhac:

1. Them xac nhan truoc khi `Xóa mẫu`.
2. Them xac nhan khi `Lưu` trung ten mau cu.
3. Hien thi ro `Mẫu đang chọn` tren man hinh phay IC.
4. Dat ten file G-code theo mau IC, vi du `IP14_CPU_P01.gcode`.
5. Them muc `Sao lưu mẫu` de copy `ic_depth.cfg`.
6. Them huong dan ngan tren tem/mat may: `Home -> Mẫu IC -> Chọn IC -> Phay`.
7. Kiem tra lai phan bu mesh neu dung de bu mat main cong/venh.
