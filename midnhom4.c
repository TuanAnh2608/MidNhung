#define DEG_TO_RAD    0.01745329f   
#define BALL_RADIUS   2             

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

    // Quy đổi map_id (1..3) sang chỉ số mảng (0..2)
    idx = (map_id >= 1 && map_id <= 3) ? (map_id - 1) : 0;

    glcd_clear();

    glcd_line(0, SCREEN_H - 1, SCREEN_W - 1, SCREEN_H - 1);

    glcd_draw_wall(idx);
    glcd_draw_target(idx);
    glcd_draw_cannon();
    glcd_draw_ball();

    glcd_dirty = 0;
}