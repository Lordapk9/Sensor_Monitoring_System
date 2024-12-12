# CÁCH HOẠT ĐỘNG CHƯƠNG TRÌNH

make để chạy chương trình
- ./server port để chạy server
- ./sensor_node port để chạy sensor node
- file log: gateway.log 
- file fifo: logFifo
- file database: sensor_data.db
- 1. sqlite3 sensor_data.db
- 2. SELECT * FROM sensor_data;

# Tổng Quan Hệ Thống
Hệ thống giám sát cảm biến bao gồm các nút cảm biến đo nhiệt độ phòng,cổng cảm biến thu thập tất cả dữ liệu cảm biến từ các nút cảm biến và cơ sở dữ liệu SQL để
lưu trữ tất cả dữ liệu cảm biến được xử lý bởi cổng cảm biến. Nút cảm biến sử dụng TCP riêng kết nối để truyền dữ liệu cảm biến đến cổng cảm biến. Cơ sở dữ liệu SQL là SQLite. Hệ thống đầy đủ được mô tả dưới đây.

![image](https://github.com/user-attachments/assets/cc60065d-1420-446e-a9f0-d8093b6ed496)


Cổng cảm biến có thể không đảm nhận số lượng cảm biến tối đa khi khởi động. Trên thực tế, số lượng cảm biến kết nối với cổng cảm biến không cố định và có thể thay đổi theo thời gian. 
Làm việc với các nút cảm biến nhúng thực không phải là một lựa chọn cho nhiệm vụ này. Vì thế, các nút cảm biến sẽ được mô phỏng trong phần mềm

## Sensor Gateway (Cổng cảm biến)
Thiết kế chi tiết hơn của cổng cảm biến được mô tả bên dưới. Trong phần sau, chúng tôi sẽ thảo luận chi tiết hơn về các yêu cầu tối thiểu của cả hai quy trình.

![image](https://github.com/user-attachments/assets/60711569-562f-45cb-aa5d-3ce87304b015)


# Các Yêu Cầu Tối Thiểu

## Yêu cầu 1
Cổng cảm biến bao gồm một tiến trình chính và một tiến trình ghi log. Tiến trình ghi log được khởi động (bằng fork) như một tiến trình con của tiến trình chính.

## Yêu cầu 2
Tiến trình chính chạy ba luồng: luồng quản lý kết nối, luồng quản lý dữ liệu và luồng quản lý lưu trữ. Một cấu trúc dữ liệu chia sẻ (xem lab 8) được sử dụng để giao tiếp giữa tất cả các luồng. Lưu ý rằng việc truy cập đọc/ghi/cập nhật vào dữ liệu chia sẻ cần phải an toàn cho luồng!

## Yêu cầu 3
Trình quản lý kết nối lắng nghe trên một socket TCP để nhận các yêu cầu kết nối đến từ các nút cảm biến mới. Số cổng của kết nối TCP này được cung cấp như một đối số dòng lệnh khi khởi động tiến trình chính, ví dụ: ./server 1234

## Yêu cầu 4
Trình quản lý kết nối bắt các gói tin đến từ các nút cảm biến như được định nghĩa trong lab 7. Tiếp theo, trình quản lý kết nối ghi dữ liệu vào cấu trúc dữ liệu chia sẻ.

## Yêu cầu 5
Luồng quản lý dữ liệu thực hiện trí thông minh của cổng cảm biến như được định nghĩa trong lab 5. Tóm lại, nó đọc các phép đo cảm biến từ dữ liệu chia sẻ, tính toán giá trị trung bình động của nhiệt độ và sử dụng kết quả đó để quyết định 'quá nóng/lạnh'. Nó không ghi các giá trị trung bình động vào dữ liệu chia sẻ - chỉ sử dụng chúng để ra quyết định nội bộ.

## Yêu cầu 6
Luồng quản lý lưu trữ đọc các phép đo cảm biến từ cấu trúc dữ liệu chia sẻ và chèn chúng vào cơ sở dữ liệu SQL (xem lab 6). Nếu kết nối đến cơ sở dữ liệu SQL thất bại, trình quản lý lưu trữ sẽ đợi một chút trước khi thử lại. Các phép đo cảm biến sẽ ở lại trong dữ liệu chia sẻ cho đến khi kết nối đến cơ sở dữ liệu hoạt động trở lại. Nếu kết nối không thành công sau 3 lần thử, cổng sẽ đóng.

## Yêu cầu 7
Tiến trình ghi log nhận các sự kiện log từ tiến trình chính sử dụng một FIFO có tên "logFifo". Nếu FIFO này không tồn tại khi khởi động tiến trình chính hoặc tiến trình log, nó sẽ được tạo bởi một trong các tiến trình. Tất cả các luồng của tiến trình chính có thể tạo ra các sự kiện log và ghi các sự kiện log này vào FIFO. Điều này có nghĩa là FIFO được chia sẻ bởi nhiều luồng và do đó, việc truy cập vào FIFO phải an toàn cho luồng.

## Yêu cầu 8
Một sự kiện log chứa một thông điệp thông tin ASCII mô tả loại sự kiện. Đối với mỗi sự kiện log nhận được, tiến trình log ghi một thông điệp ASCII có định dạng <số thứ tự> <timestamp> <thông điệp thông tin sự kiện log> vào một dòng mới trên tệp log có tên "gateway.log".

## Yêu cầu 9
Ít nhất các sự kiện log sau cần được hỗ trợ:

1. Từ trình quản lý kết nối:
   - Một nút cảm biến với <sensorNodeID> đã mở một kết nối mới
   - Nút cảm biến với <sensorNodeID> đã đóng kết nối

2. Từ trình quản lý dữ liệu:
   - Nút cảm biến với <sensorNodeID> báo cáo quá lạnh (nhiệt độ trung bình = <giá trị>)
   - Nút cảm biến với <sensorNodeID> báo cáo quá nóng (nhiệt độ trung bình = <giá trị>)
   - Nhận dữ liệu cảm biến với ID nút cảm biến không hợp lệ <node-ID>

3. Từ trình quản lý lưu trữ:
   - Kết nối đến máy chủ SQL đã được thiết lập
   - Bảng mới <tên-bảng> đã được tạo
   - Kết nối đến máy chủ SQL bị mất
   - Không thể kết nối đến máy chủ SQL
