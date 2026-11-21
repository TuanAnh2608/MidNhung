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
#define GRAVITY 0.05f // Gia tốc trọng trường giả lập
#define BOUNCE_FACTOR 0.75f // Hệ số bảo toàn vận tốc khi bóng nảy tường

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

// buf cho 2 hàng lcd
char lcd_buf[17];
char lcd_buf2[17];

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

volatile unsigned char digit[4];   // digit[0..1] = điểm (chục, đơn vị), digit[2..3] = số phát

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
// NHÓM 2 - GIAO TIẾP ĐẦU VÀO
// input_update() được gọi một lần mỗi tick, khoảng 20 ms.
#define INPUT_REPEAT_DELAY 20  // Giữ BT2/BT3 khoảng 400 ms trước khi lặp
#define INPUT_REPEAT_STEP   5  // Lặp mỗi 100 ms. Đổi thành 3 nếu muốn 60 ms.

typedef struct {
    unsigned char previous;  // Mẫu đọc ở tick trước
    unsigned char stable;    // Trạng thái đã qua chống dội
} InputFilter;

static InputFilter button_filter, keypad_filter;
static unsigned char repeat_ticks;

// Hai mẫu liên tiếp giống nhau mới xác nhận nhấn hoặc nhả.
// Trả về mã nút/phím khi vừa nhấn, còn lại trả về 0.
static unsigned char press_event(InputFilter *filter, unsigned char sample)
{
    if (sample == filter->previous && sample != filter->stable) {
        filter->stable = sample;
        return sample;
    }

    filter->previous = sample;
    return 0;
}

void input_init(void)
{
    // BT1 = PB2, BT2 = PB3, BT3 = PB0. Nhấn đọc được mức 0.
    DDRB.2 = 0; DDRB.3 = 0; DDRB.0 = 0;
    PORTB.2 = 1; PORTB.3 = 1; PORTB.0 = 1;

    // Cột ma trận: PC4, PC5, PE0 là đầu vào có điện trở kéo lên.
    DDRC.4 = 0; DDRC.5 = 0; DDRE.0 = 0;
    PORTC.4 = 1; PORTC.5 = 1; PORTE.0 = 1;

    // Hàng ma trận: PC6, PE1, PD0.
    // Khi nghỉ, đặt hàng ở chế độ input và tắt pull-up.
    // Khi quét, chỉ cần đổi DDR của hàng được chọn sang output để xuất 0.
    DDRC.6 = 0; DDRE.1 = 0; DDRD.0 = 0;
    ROW1 = 0; ROW2 = 0; ROW3 = 0;

    button_filter.previous = button_filter.stable = 0;
    keypad_filter.previous = keypad_filter.stable = 0;
    btn_event = key_event = repeat_ticks = 0;
}

// Bố trí ma trận:
//   Hàng 1: 1 2 3
//   Hàng 2: 4 5 6
//   Hàng 3: 7 8 9
// Trả về 0 nếu không nhấn hoặc phát hiện nhiều phím.
unsigned char keypad_scan(void)
{
    unsigned char row, columns, key = 0;

    for (row = 0; row < 3; ++row) {
        // Chọn một hàng và kéo hàng đó xuống mức 0.
        switch (row) {
        case 0: DDRC.6 = 1; break;
        case 1: DDRE.1 = 1; break;
        case 2: DDRD.0 = 1; break;
        }

        delay_us(10);

        columns = 0;
        if (!COL1) columns |= 1;
        if (!COL2) columns |= 2;
        if (!COL3) columns |= 4;

        // Trả hàng về trạng thái trở kháng cao trước khi quét hàng khác.
        switch (row) {
        case 0: DDRC.6 = 0; break;
        case 1: DDRE.1 = 0; break;
        case 2: DDRD.0 = 0; break;
        }

        if (columns == 0) continue;

        // Đã thấy phím ở hàng trước hoặc có nhiều cột cùng được nhấn.
        if (key || (columns & (columns - 1))) return 0;

        if (columns == 1)      key = row * 3 + 1;
        else if (columns == 2) key = row * 3 + 2;
        else                   key = row * 3 + 3;
    }

    return key;
}

void input_update(void)
{
    unsigned char button = 0;

    // Nếu nhấn nhiều nút đơn, ưu tiên BT1 rồi đến BT2 và BT3.
    if (!BT1)      button = 1;
    else if (!BT2) button = 2;
    else if (!BT3) button = 3;

    btn_event = press_event(&button_filter, button);

    if (btn_event == 2 || btn_event == 3) {
        // BT2/BT3 vừa được nhấn: phát sự kiện ngay sau chống dội.
        repeat_ticks = INPUT_REPEAT_DELAY;
    } else if (button == button_filter.stable &&
               (button == 2 || button == 3)) {
        // BT2/BT3 đang được giữ: phát lại khi bộ đếm về 0.
        if (--repeat_ticks == 0) {
            btn_event = button;
            repeat_ticks = INPUT_REPEAT_STEP;
        }
    }

    // Phím ma trận chỉ phát sự kiện khi vừa nhấn.
    key_event = press_event(&keypad_filter, keypad_scan());
}

// map_load() do nhóm 3 định nghĩa phía dưới, cần khai báo trước để gọi được
void map_load(unsigned char id);

void handle_keypad(void)
{
    unsigned char key = key_event;
    key_event = 0;

    // Phím 9 cho phép reset ở mọi trạng thái.
    if (key == 9) {
        game_reset();
        return;
    }

    // Chỉ đổi lực, bản đồ hoặc chế độ khi đang ngắm.
    if (game_state != ST_AIM) return;

    switch (key) {
    case 1: case 2: case 3:
        power_lvl = key;
        break;

    case 4: case 5: case 6:
        map_load(key - 3);  // Phím 4/5/6 tương ứng bản đồ 1/2/3
        break;

    case 7:
        auto_mode = !auto_mode;
        state_timer = 0;
        break;

    default:
        return;  // Phím 8 hoặc không có sự kiện
    }

    lcd_dirty = glcd_dirty = 1;
}


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
void feedback_init(void) {
    // LCD 16x2
    lcd_init(16);
    lcd_clear();
    lcd_gotoxy(0, 0);

    // Led 7 doan
    DDRF.0 = 1; DDRF.3 = 1; DDRF.4 = 1; DDRF.6 = 1; // Chân chọn các ô led -> output
    DDRD.3 = 1; DDRF.1 = 1; DDRF.2 = 1; DDRF.5 = 1; DDRF.7 = 1; DDRA.7 = 1; DDRD.1 = 1; DDRD.2 = 1; // Chân điều khiển các đoạn và dấu chấm -> out

    PORTF.0 = 1; PORTF.3 = 1; PORTF.4 = 1; PORTF.6 = 1; // Tắt cả 4 vị trí LED khởi tạo (anot chung cho vị trí led)
    PORTF.1 = 0; PORTF.5 = 0; PORTF.2 = 0; PORTA.7 = 0; PORTD.1 = 0; PORTD.2 = 0; PORTD.3 = 0; PORTF.7 = 0; // Tắt các đoạn a-g (catot chung cho các đoạn trong led)

    // LED RGB
    DDRD.4 = 1; DDRD.5 = 1; DDRD.6 = 1; //out
    PORTD.4 = 0; PORTD.5 = 0; PORTD.6 = 0; //tat

    // RELAY
    DDRC.3 = 1;
    PORTC.3 = 0;
}


// Cập nhật 2 dòng thông tin lên LCD 16x2 (Dòng 1: Mode, Góc, Map; Dòng 2: Lực, Trúng/Trượt)
void lcd_update_status(void) {
    if (lcd_dirty) {
        lcd_clear();

        // Dong 1
        lcd_gotoxy(0, 0);
        char mode = 'M';
        if (auto_mode == 1)
            mode = 'A';
        sprintf(lcd_buf, "M:%c A:%d Map:%d", mode, angle, map_id);
        lcd_puts(lcd_buf);

        // Dong 2
        lcd_gotoxy(0, 1);
        flash char *rsl = "CHO";
        if (last_result == 1)
            rsl = "TRUNG";
        else if (last_result == 2)
            rsl = "TRUOT";

        sprintf(lcd_buf2, "Luc: %d %s", power_lvl, rsl);
        lcd_puts(lcd_buf2);

        lcd_dirty = 0;
    }
}

// Quét 1 chu kỳ 4 LED 7 đoạn (2 LED trái hiện điểm số, 2 LED phải hiện số lượt bắn)
//                               0     1     2     3     4     5     6     7     8     9
flash unsigned char seg7[10] = {0xFC, 0x60, 0xDA, 0xF2, 0x66, 0xB6, 0xBE, 0xE0, 0xFE, 0xF6};

void led7_scan_once(unsigned char pos)
{
    unsigned char s = seg7[digit[pos]];

    // Tắt cả 4 LED trước (chân chọn tích cực mức 0) để khỏi bóng mờ
    PORTF.0 = 1; PORTF.3 = 1; PORTF.4 = 1; PORTF.6 = 1;

    // Mỗi bit ứng với 1 đoạn, mỗi đoạn là 1 chân
    PORTF.1 = (s & 0x80) ? 1 : 0;   // a
    PORTF.5 = (s & 0x40) ? 1 : 0;   // b
    PORTA.7 = (s & 0x20) ? 1 : 0;   // c
    PORTD.1 = (s & 0x10) ? 1 : 0;   // d
    PORTD.2 = (s & 0x08) ? 1 : 0;   // e
    PORTF.2 = (s & 0x04) ? 1 : 0;   // f
    PORTD.3 = (s & 0x02) ? 1 : 0;   // g
    PORTF.7 = (s & 0x01) ? 1 : 0;   // dp

    // 3. Bật đúng 1 LED
    switch (pos) {
        case 0: PORTF.0 = 0; break;
        case 1: PORTF.3 = 0; break;
        case 2: PORTF.4 = 0; break;
        case 3: PORTF.6 = 0; break;
    }
}

void led7_set(unsigned char sc, unsigned char sh)
{
    if (sc > 99) sc = 99;
    if (sh > 99) sh = 99;
    digit[0] = sc / 10;  digit[1] = sc % 10;
    digit[2] = sh / 10;  digit[3] = sh % 10;
}


// TRÌNH PHỤC VỤ NGẮT TIMER0 (CHẠY MỖI 2ms)

// Mục đích: 
// - Cứ mỗi 2ms bật sáng luân phiên 1 trong 4 LED 7 đoạn (8ms đủ 1 vòng, không bao giờ nháy)
// - Cứ 10 lần ngắt (20ms) bật tick_flag = 1, giảm state_timer và tự ngắt Relay khi hết relay_timer.
volatile unsigned char tick_flag = 0;
// Khởi tạo Timer0 chế độ ngắt tràn (xung nội 8MHz)
void timer0_ovf_init(void) {
    // Chọn prescaler = 1024 (1 tick = 1024 / 8,000,000s = 128us)
    TCCR0 = (1 << CS02) | (1 << CS01) | (1 << CS00);
    TCNT0 = 0xF0;          // 256 - 16 = 240 -> ~2.05ms mỗi lần tràn
    TIMSK |= (1 << TOIE0); // Cho phép ngắt tràn Timer0
    #asm("sei")            // Bật ngắt toàn cục
}

// ISR ngắt tràn Timer0: quét 1 LED mỗi lần ngắt, 10 lần (~20ms) bật tick_flag
interrupt [TIM0_OVF] void timer0_ovf_isr(void) {
    static unsigned char digit_idx = 0;
    static unsigned char sub_tick  = 0;

    TCNT0 = 0xF0; // Nạp lại giá trị khởi đầu cho chu kỳ tràn tiếp theo

    led7_scan_once(digit_idx);
    digit_idx = (digit_idx + 1) & 3;

    if (++sub_tick >= 10) {
        sub_tick  = 0;
        tick_flag = 1;
    }
}

// Điều khiển màu LED RGB (0: Xanh dương sẵn sàng, 1: Xanh lá trúng, 2: Đỏ trượt)
void rgb_set_state(unsigned char state) {
    LED_R = 0; LED_G = 0; LED_B = 0;

    if (state == 0) {
        LED_B = 1;
    } else if (state == 1) {
        LED_G = 1;
    } else if (state == 2) {
        LED_R = 1;
    }
}


// 6. HÀM MAIN (VÒNG LẶP CHÍNH)
// Cần check logic lại nhé. logic bên dưới chỉ là gợi ý.
void main(void) {
    // Gọi các hàm init thiết lập trạng thái ban đầu
    
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
