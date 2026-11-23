// 1. KHAI BÁO THƯ VIỆN
#include <mega128.h>   
#include <glcd.h>      
#include <alcd.h>      
#include <font5x7.h>   
#include <delay.h>     
#include <math.h>      // Thư viện toán học sin(), cos() để tính quỹ đạo ném xiên
#include <stdio.h>     

// 2. ĐỊNH NGHĨA CHÂN PHẦN CỨNG (Cần check đúng port & thay đổi nếu cần)
// Code AI gen nên nhớ kiểm tra PORT

// Nút bấm độc lập
#define BT1 PINB.2   // Nút bắn
#define BT2 PINB.3   // Tăng góc nâng nòng pháo
#define BT3 PINB.0   // Giảm góc 

// Bàn phím ma trận 3x3
#define ROW1 PORTC.6    // Hàng 1 
#define ROW2 PORTE.1    // Hàng 2 
#define ROW3 PORTD.0    // Hàng 3 
#define COL1 PINC.4     // Cột 1 
#define COL2 PINC.5     // Cột 2 
#define COL3 PINE.0     // Cột 3 

// Rơ-le, LED 3 màu, Đèn nền LCD
#define RELAY1 PORTC.3  // Đóng/ngắt tạo tiếng "tạch" mô phỏng tiếng bắn
#define LED_R  PORTD.4  // LED Đỏ: Bắn trượt
#define LED_G  PORTD.5  // LED Xanh lá: Trúng mục tiêu
#define LED_B  PORTD.6  // LED Xanh dương: Sẵn sàng bắn
#define LCD_BL PORTD.7  // Cấp nguồn bật đèn nền màn hình LCD/GLCD

// --- Hằng số Máy trạng thái (State Machine) ---
#define ST_AIM    0     // Đang ngắm bắn (chờ điều chỉnh góc/lực hoặc Auto quét)
#define ST_FLY    1     // Quả bóng đang bay trên không
#define ST_RESULT 2     // Hiển thị kết quả TRUNG/TRUOT & sáng đèn RGB (~1s)

// --- Hằng số Vật lý & Kích thước màn hình (Hệ tọa độ vật lý: Y hướng LÊN) ---
#define SCREEN_W      84
#define SCREEN_H      48
#define CANNON_X      6     // Tọa độ X tâm bệ pháo
#define CANNON_Y      5     // Tọa độ Y vật lý tâm bệ pháo (cách đáy 5px)
#define BARREL_LEN    12    // Chiều dài nòng pháo (pixel)
#define GRAVITY       0.05f // Gia tốc trọng trường giả lập
#define BOUNCE_FACTOR 0.75f // Hệ số bảo toàn vận tốc khi bóng nảy tường

#define DEG_TO_RAD    0.01745329f   
#define BALL_RADIUS   2             

// 3. BIẾN TOÀN CỤC & MẢNG DỮ LIỆU DÙNG CHUNG
// Sự kiện đầu vào (cập nhật mỗi tick bởi input_update)
unsigned char btn_event = 0;   // 0: Không, 1: BT1 (Bắn), 2: BT2 (+Góc), 3: BT3 (-Góc)
unsigned char key_event = 0;   // 0: Không, 1..9: Phím ma trận vừa bấm
unsigned char lcd_dirty = 1;   // 0: K có dữ liệu mới thay đổi để hiển thị lcd, 1: Cần cập nhật màn lcd
unsigned char glcd_dirty = 1;  // 0: K có dữ liệu mới thay đổi để hiển thị glcd, 1: Cần cập nhật màn glcd

// Trạng thái trò chơi & thông số
unsigned char game_state   = ST_AIM;
unsigned char auto_mode    = 0;  // 0: Manual, 1: Auto
unsigned char map_id       = 1;  // 1 / 2 / 3
unsigned char last_result  = 0;  // 0: Chờ, 1: TRUNG, 2: TRUOT
volatile unsigned char  state_timer  = 0;  // Bộ đếm tick dùng cho ST_RESULT và Auto
volatile unsigned char  relay_timer  = 0;  // Bộ đếm tick tắt Relay không dùng delay cứng

int angle           = 45;        // Góc nâng nòng (15 -> 75 độ)
char auto_angle_dir = 1;         // Hướng quét góc ở chế độ Auto (+1 / -1)
unsigned char power_lvl = 2;     // Mức lực: 1, 2, 3
unsigned char score     = 0;     // Điểm số (2 LED trái)
unsigned char shots     = 0;     // Số phát đã bắn (2 LED phải)

// Trạng thái vật lý động (Hệ tọa độ vật lý: Y hướng LÊN)
float ball_x, ball_y;            // Tọa độ tâm bóng
float vx, vy;                    // Vận tốc bóng (vy > 0 là bay lên, vy < 0 là rơi xuống)
int target_y        = 10;        // Cao độ Y vật lý hiện tại của rổ mục tiêu
char target_dir     = 1;         // Hướng di chuyển của rổ ở Map 3

// Bảng vận tốc ban đầu v0 theo mức lực (index 1..3)
flash float V0_TABLE[4] = {0.0f, 1.4f, 1.9f, 2.4f};

// Quy ước tọa độ vật lý: (x, y) là góc DƯỚI-TRÁI của vật thể, Y hướng LÊN (0..47)
typedef struct {
    unsigned char has_wall;      // 1: Có tường chắn giữa sân, 0: Không có
    int wall_x, wall_y;          // Tọa độ góc dưới-trái của tường (Y vật lý hướng lên)
    int wall_w, wall_h;          // Chiều rộng & chiều cao tường
    int basket_x;                // Tọa độ X mép trái của rổ mục tiêu
    int basket_w, basket_h;      // Chiều rộng & chiều cao của rổ mục tiêu
    int basket_min_y;            // Biên dưới cao độ Y của rổ (dùng cho Map 3)
    int basket_max_y;            // Biên trên cao độ Y của rổ (dùng cho Map 3)
    unsigned char is_moving;     // 1: Rổ di chuyển lên/xuống (Map 3), 0: Đứng yên
} MapConfig;

// Bảng cấu hình 3 bản đồ lưu trong Flash (Dùng chung cho cả Physics và GLCD)
flash MapConfig MAPS[3] = {
    // has_wall, wx, wy, ww, wh, bx, bw, bh, min_y, max_y, is_moving
    {0,  0, 0, 0,  0, 70, 11, 4, 10, 10, 0}, // Map 1: Bia tĩnh tại y = 10
    {1, 41, 0, 3, 24, 70, 11, 4, 10, 10, 0}, // Map 2: Tường giữa cao 24px, bia tĩnh tại y = 10
    {0,  0, 0, 0,  0, 70, 11, 4,  6, 36, 1}  // Map 3: Bia di động chạy từ y = 6 đến y = 36
};

// 4. HÀM & MỤC ĐÍCH CHI TIẾT (FUNCTION PROTOTYPES)

// --- NHÓM 1: KHỞI TẠO & ĐIỀU PHỐI HỆ THỐNG ---
// Khởi tạo (Input, GLCD, LCD, LED, Relay, ...)
void system_init(void);

// Reset điểm số, số lượt bắn, góc bắn và màn hình về ban đầu
void game_reset(void);

// Thực hiện chuỗi hành động khi khai hỏa (tăng lượt bắn, kích Relay, phóng bóng)
void trigger_fire(void);

// Xử lý nhịp hoạt động của chế độ Auto (đếm tick để quét góc lên/xuống, đủ nhịp thì gọi trigger_fire)
void auto_step(void);


// --- NHÓM 2: GIAO TIẾP ĐẦU VÀO ---
// Cấu hình nút đơn & ma trận phím
void input_init(void);

// Chạy mỗi tick để quét BT1..3 (có hỗ trợ giữ nút BT2/BT3 để lặp tăng/giảm góc) và ma trận 3x3, lưu vào btn_event & key_event
void input_update(void);

// Quét bàn phím ma trận 3x3 (trả về số 1..9 tương ứng phím bấm, 0 nếu không bấm)
unsigned char keypad_scan(void);

// Giải mã key_event (1-3: chỉnh power_lvl, 4-6: gọi map_load, 7: đổi Manual/Auto, 9: gọi game_reset)
void handle_keypad(void);


// --- NHÓM 3: TÍNH TOÁN VẬT LÝ & VA CHẠM (PHYSICS) ---
// Nạp thông số bản đồ mới (id = 0, 1, 2) từ bảng MAPS[] và đặt lại vị trí target_y ban đầu
void map_load(unsigned char id);

// Cộng/trừ góc nâng nòng pháo theo tham số delta và giới hạn trong [15, 75] độ
void physics_set_angle(int delta);

// Tính tọa độ xuất phát tại đầu nòng và vận tốc ban đầu (vx, vy) dựa theo góc & lực
void physics_launch_ball(void);

// Cập nhật tọa độ (ball_x, ball_y) theo trọng lực và xử lý nảy khi chạm trần/tường
void physics_update_ball(void);

// Cập nhật tọa độ di chuyển lên/xuống của bia mục tiêu khi người chơi chọn Map 3
void physics_update_target(void);

// Kiểm tra trạng thái quả bóng (0: đang bay, 1: rơi trúng rổ, 2: bắn trượt/chạm đất)
unsigned char physics_check_collision(void);


// --- NHÓM 4: HIỂN THỊ ĐỒ HỌA CHIẾN TRƯỜNG TRÊN GLCD (84x48) ---
// Cấu hình thông số vật lý (font, contrast, bias) và xóa trắng màn hình GLCD
void glcd_ui_init(void);

// Xóa khung cũ và vẽ lại toàn bộ bệ pháo, nòng pháo, tường chắn, rổ và bóng lên GLCD
void glcd_render_frame(void);


// --- NHÓM 5: HIỂN THỊ PHỤ & TÍN HIỆU BÁO HIỆU (LCD, LED 7 ĐOẠN, RELAY, RGB) ---
// Khởi tạo màn hình LCD 16x2, cấu hình chân xuất ra LED 7 đoạn, LED 3 màu và Rơ-le
void feedback_init(void);

// Cập nhật 2 dòng thông tin lên LCD 16x2 (Dòng 1: Mode, Góc, Map; Dòng 2: Lực, Trúng/Trượt)
void lcd_update_status(void);

// Quét 1 chu kỳ 4 LED 7 đoạn (2 LED trái hiện điểm số, 2 LED phải hiện số lượt bắn)
void led7_scan_once(unsigned char pos);

// Điều khiển màu LED RGB (0: Xanh dương sẵn sàng, 1: Xanh lá trúng, 2: Đỏ trượt)
void rgb_set_state(unsigned char state);


// --- TRIỂN KHAI CÁC HÀM HIỂN THỊ GLCD (NHÓM 4) ---

// Chuyển đổi tọa độ Y vật lý (đáy-đỉnh => đỉnh-đáy)
static int phys_to_glcd_y(int py) {
    int gy = (SCREEN_H - 1) - py;
    if (gy < 0) gy = 0;
    if (gy >= SCREEN_H) gy = SCREEN_H - 1;
    return gy;
}

// Giới hạn tọa độ X trong vùng hiển thị hợp lệ [0 .. SCREEN_W - 1]
static int clamp_glcd_x(int gx) {
    if (gx < 0) return 0;
    if (gx >= SCREEN_W) return SCREEN_W - 1;
    return gx;
}

// 1. Cấu hình thông số vật lý (font, contrast, bias) và xóa trắng màn hình GLCD
void glcd_ui_init(void) {
    GLCDINIT_t glcd_init_data;

    // Bật đèn nền màn hình LCD/GLCD
    LCD_BL = 1;

    // Cấu hình bộ điều khiển PCD8544 (Nokia 5110) trong CodeVisionAVR
    glcd_init_data.font      = font5x7;
    glcd_init_data.readxmem  = NULL;
    glcd_init_data.writexmem = NULL;
    glcd_init_data.temp_coef = PCD8544_DEFAULT_TEMP_COEF; // Hệ số bù nhiệt (mặc định = 2)
    glcd_init_data.bias      = PCD8544_DEFAULT_BIAS;      // Điện áp phân cực (mặc định = 4)
    glcd_init_data.vlcd      = PCD8544_DEFAULT_VLCD;      // Độ tương phản (mặc định ~ 60-66)

    glcd_init(&glcd_init_data);
    glcd_clear();
    glcd_dirty = 1;
}

// Hàm phụ trợ: Vẽ bệ pháo, nòng pháo xoay theo góc bắn và 2 điểm dẫn hướng
static void glcd_draw_cannon(void) {
    float rad;
    int base_gx, base_gy;
    int tip_gx, tip_gy;
    int dot_x, dot_y;

    rad     = (float)angle * DEG_TO_RAD;
    base_gx = CANNON_X;
    base_gy = phys_to_glcd_y(CANNON_Y);

    // Tính tọa độ đầu nòng pháo (làm tròn +0.5f để giảm sai số số nguyên)
    tip_gx = clamp_glcd_x((int)(CANNON_X + BARREL_LEN * cos(rad) + 0.5f));
    tip_gy = phys_to_glcd_y((int)(CANNON_Y + BARREL_LEN * sin(rad) + 0.5f));

    // Vẽ bệ pháo (thân tròn xoay + chân đế đặt sát mặt đất)
    glcd_fillcircle(base_gx, base_gy, 4);
    glcd_bar(1, base_gy + 1, 11, SCREEN_H - 1);

    // Vẽ nòng pháo dày 2 pixel để hiển thị rõ nét trên màn hình đơn sắc
    glcd_line(base_gx, base_gy, tip_gx, tip_gy);
    if (base_gy > 0 && tip_gy > 0) {
        glcd_line(base_gx, base_gy - 1, tip_gx, tip_gy - 1);
    }

    // Khi đang ở trạng thái ngắm bắn (ST_AIM), vẽ thêm 2 điểm dẫn hướng đầu nòng
    if (game_state == ST_AIM) {
        dot_x = (int)(CANNON_X + (BARREL_LEN + 4) * cos(rad) + 0.5f);
        dot_y = (SCREEN_H - 1) - (int)(CANNON_Y + (BARREL_LEN + 4) * sin(rad) + 0.5f);
        if (dot_x >= 0 && dot_x < SCREEN_W && dot_y >= 0 && dot_y < SCREEN_H) {
            glcd_setpixel(dot_x, dot_y);
        }

        dot_x = (int)(CANNON_X + (BARREL_LEN + 8) * cos(rad) + 0.5f);
        dot_y = (SCREEN_H - 1) - (int)(CANNON_Y + (BARREL_LEN + 8) * sin(rad) + 0.5f);
        if (dot_x >= 0 && dot_x < SCREEN_W && dot_y >= 0 && dot_y < SCREEN_H) {
            glcd_setpixel(dot_x, dot_y);
        }
    }
}

// Hàm phụ trợ: Vẽ tường chắn chướng ngại vật (Map 2)
static void glcd_draw_wall(unsigned char idx) {
    int wx0, wx1, wy0, wy1;

    if (!MAPS[idx].has_wall) return;

    wx0 = clamp_glcd_x(MAPS[idx].wall_x);
    wx1 = clamp_glcd_x(MAPS[idx].wall_x + MAPS[idx].wall_w - 1);
    wy1 = phys_to_glcd_y(MAPS[idx].wall_y);
    wy0 = phys_to_glcd_y(MAPS[idx].wall_y + MAPS[idx].wall_h - 1);

    glcd_bar(wx0, wy0, wx1, wy1);
}

// Hàm phụ trợ: Vẽ bia mục tiêu dạng hình tròn đồng tâm
static void glcd_draw_target(unsigned char idx) {
    int cx, cy, r;

    r = MAPS[idx].basket_w / 2;
    if (r < 2) r = 2;

    cx = clamp_glcd_x(MAPS[idx].basket_x + r);
    cy = phys_to_glcd_y(target_y + (MAPS[idx].basket_h / 2));

    if (cx < r) cx = r;
    if (cx >= SCREEN_W - r) cx = SCREEN_W - 1 - r;
    if (cy < r) cy = r;
    if (cy >= SCREEN_H - r) cy = SCREEN_H - 1 - r;

    glcd_circle(cx, cy, r);
    if (r >= 4) {
        glcd_circle(cx, cy, r / 2);
    }
    glcd_setpixel(cx, cy);
}

// Hàm phụ trợ: Vẽ quả bóng bàn khi đang bay (ST_FLY) hoặc vừa chạm đích (ST_RESULT)
static void glcd_draw_ball(void) {
    int gx, gy;

    if (game_state != ST_FLY && game_state != ST_RESULT) return;

    gx = (int)(ball_x + 0.5f);
    gy = (SCREEN_H - 1) - (int)(ball_y + 0.5f);

    if (gx >= 0 && gx < SCREEN_W && gy >= 0 && gy < SCREEN_H) {
        if (gx < BALL_RADIUS) gx = BALL_RADIUS;
        if (gx >= SCREEN_W - BALL_RADIUS) gx = SCREEN_W - 1 - BALL_RADIUS;
        if (gy < BALL_RADIUS) gy = BALL_RADIUS;
        if (gy >= SCREEN_H - BALL_RADIUS) gy = SCREEN_H - 1 - BALL_RADIUS;

        glcd_fillcircle(gx, gy, BALL_RADIUS);
    }
}

void glcd_render_frame(void) {
    unsigned char idx;
    idx = (map_id >= 1 && map_id <= 3) ? (map_id - 1) : 0;

    glcd_clear();

    glcd_line(0, SCREEN_H - 1, SCREEN_W - 1, SCREEN_H - 1);

    glcd_draw_wall(idx);
    glcd_draw_target(idx);
    glcd_draw_cannon();
    glcd_draw_ball();

    glcd_dirty = 0;
}

// 5. TRÌNH PHỤC VỤ NGẮT TIMER0 (CHẠY MỖI 2ms)

// Mục đích: 
// - Cứ mỗi 2ms bật sáng luân phiên 1 trong 4 LED 7 đoạn (8ms đủ 1 vòng, không bao giờ nháy)
// - Cứ 10 lần ngắt (20ms) bật tick_flag = 1, giảm state_timer và tự ngắt Relay khi hết relay_timer.
volatile unsigned char tick_flag = 0;
interrupt [TIM0_COMP] void timer0_comp_isr(void) {
    volatile static unsigned char digit_idx = 0; // Vị trí LED 7 đoạn đang quét (0..3)
    volatile static unsigned char sub_tick  = 0; // Đếm 10 lần x 2ms = 20ms

    // 1. Quét 1 LED 7 đoạn tại vị trí digit_idx (gọi hàm xuất bit)
    led7_scan_once(digit_idx);
    if (++digit_idx >= 4) digit_idx = 0;

    // 2. Tạo nhịp chuẩn 20ms cho toàn bộ logic game
    if (++sub_tick >= 10) {
        sub_tick = 0;
        tick_flag = 1;

        if (state_timer > 0) state_timer--;
        if (relay_timer > 0) {
            relay_timer--;
            if (relay_timer == 0) RELAY1 = 0; // Tự động ngắt Rơ-le sau khi kêu "tạch"
        }
    }
}

// 6. HÀM MAIN (VÒNG LẶP CHÍNH) (Này chỉ là gợi ý, chưa chắc là logic chính xác)
void main(void) {
    // 1. Gọi system_init() và game_reset() thiết lập trạng thái ban đầu
    
    while (1) {
        // --- NHỊP HỆ THỐNG (Mỗi vòng lặp ~8-10ms nhờ quét LED 7 đoạn) ---
        // 1. Quét LED 7 đoạn & cập nhật bộ đếm tắt Relay (nếu relay_timer > 0) (Chạy trong code ISR)
        // 2. Gọi input_update() và handle_keypad()
        
        // --- MÁY TRẠNG THÁI ---
        // Case ST_AIM:
        //   - Cập nhật bia di động (physics_update_target)
        //   - Nếu Manual: xử lý btn_event (BT2 tăng góc, BT3 giảm góc, BT1 gọi trigger_fire)
        //   - Nếu Auto: gọi auto_step()
        
        // Case ST_FLY:
        //   - Cập nhật vật lý bóng & bia, vẽ lại GLCD
        //   - Gọi physics_check_collision():
        //     + Nếu Trúng (1): score++, last_result = 1, rgb_set_state(1), state_timer = 50 (~1s), chuyển sang ST_RESULT
        //     + Nếu Trượt (2): last_result = 2, rgb_set_state(2), state_timer = 100 (~2s), chuyển sang ST_RESULT
        
        // Case ST_RESULT:
        //   - Giảm state_timer mỗi tick; khi state_timer == 0 thì bật lại LED xanh dương (rgb_set_state(0)) và quay về ST_AIM
    }
}
