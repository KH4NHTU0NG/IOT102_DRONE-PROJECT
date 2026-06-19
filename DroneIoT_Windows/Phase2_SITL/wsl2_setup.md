# ============================================================
# wsl2_setup.md — Windows: Hướng dẫn bật WSL2 để chạy SITL
# ============================================================

# Thiết lập WSL2 cho ArduPilot SITL trên Windows

> **Lý do bắt buộc dùng WSL2**: ArduPilot SITL yêu cầu môi trường Linux.
> Trên Windows, WSL2 là lựa chọn chính thức được ArduPilot hỗ trợ.

## Bước 1: Bật WSL2 (chạy PowerShell với quyền Admin)

```powershell
# Mở PowerShell as Administrator, chạy:
wsl --install
# Hoặc nếu đã có WSL1:
wsl --set-default-version 2
```

Khởi động lại máy tính sau khi cài.

## Bước 2: Cài Ubuntu 22.04 từ Microsoft Store

1. Mở **Microsoft Store**
2. Tìm `Ubuntu 22.04 LTS`
3. Cài đặt và mở lần đầu
4. Tạo username/password cho Linux user

## Bước 3: Verify WSL2 hoạt động

```powershell
# Trong PowerShell:
wsl --list --verbose
# Phải thấy: Ubuntu-22.04  Running  2
```

## Bước 4: Chạy script cài SITL trong WSL2

Sau khi WSL2 Ubuntu sẵn sàng, chạy:

```powershell
# Từ PowerShell, chạy script:
.\install_sitl.ps1
```

## Lưu ý quan trọng — Network trong WSL2

- SITL chạy trong WSL2 nhưng **QGroundControl và Docker chạy trên Windows host**
- WSL2 NAT mặc định dùng IP riêng (thường `172.x.x.x`)
- Script `run_sitl.ps1` đã xử lý tự động việc bind đúng địa chỉ

## Lỗi thường gặp

| Lỗi | Fix |
|-----|-----|
| `WSL 2 requires an update to its kernel component` | Tải và cài: https://aka.ms/wsl2kernel |
| `Virtual machine platform not enabled` | Chạy: `dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all` |
| Ubuntu không mở được | Tắt Hyper-V rồi bật lại trong Windows Features |
