// ============================================================================
// 1. KHAI BAO THU VIEN (Board QG.20.07 - ATmega128 - CodeVisionAVR)
// ============================================================================
#include <mega128.h>
#include <alcd.h>
#include <delay.h>
#include <math.h>
#include <stdio.h>

// ============================================================================
// 2. DINH NGHIA CHAN PHAN CUNG
// ============================================================================
#define BT1     PINB.2   // PB2: Nut ban
#define BT2     PINB.3   // PB3: Tang goc nang nong phao (+Goc)
#define BT3     PINB.0   // PB0: Giam goc nang nong phao (-Goc)

#define NOKIA_DC_BIT   0 // PG0
#define NOKIA_DIN_BIT  1 // PG1
#define NOKIA_CLK_BIT  2 // PG2
#define NOKIA_RST_BIT  3 // PG3
#define NOKIA_CE_BIT   4 // PG4

#define RELAY1  PORTC.3  // PC3: Ro-le 1
#define LED_R   PORTD.4  // PD4: LED Do (Ban truot)
#define LED_G   PORTD.5  // PD5: LED Xanh la (Ban trung)
#define LED_B   PORTD.6  // PD6: LED Xanh duong (San sang ban)
#define LCD_BL  PORTD.7  // PD7: Den nen LCD 16x2 & Nokia 5110

#define ST_AIM    0
#define ST_FLY    1
#define ST_RESULT 2

#define SCREEN_W      84
#define SCREEN_H      48
#define CANNON_X      6
#define CANNON_Y      5
#define BARREL_LEN    12
#define GRAVITY       0.145f
#define BOUNCE_FACTOR 0.75f

// ============================================================================
// 3. BIEN TOAN CUC & MANG DU LIEU DUNG CHUNG
// ============================================================================
unsigned char btn_event  = 0;
unsigned char key_event  = 0;
unsigned char lcd_dirty  = 1;
unsigned char glcd_dirty = 1;

unsigned char game_state   = ST_AIM;
unsigned char auto_mode    = 0;
unsigned char map_id       = 1;
unsigned char last_result  = 0;
volatile unsigned char state_timer = 0;
volatile unsigned char relay_timer = 0;

int angle                  = 45;
signed char auto_angle_dir = 1;
unsigned char power_lvl    = 2;
unsigned char score        = 0;
unsigned char shots        = 0;

float ball_x, ball_y;
float vx, vy;
int target_y           = 10;
signed char target_dir = 1;

flash float V0_TABLE[4] = {0.0f, 2.4f, 3.2f, 4.1f};

char lcd_buf[17];
char lcd_buf2[17];

typedef struct {
    unsigned char has_wall;
    int wall_x, wall_y;
    int wall_w, wall_h;
    int basket_x;
    int basket_w, basket_h;
    int basket_min_y;
    int basket_max_y;
    unsigned char is_moving;
} MapConfig;

flash MapConfig MAPS[3] = {
    {0,  0, 0, 0,  0, 70, 11, 6, 10, 10, 0}, // Map 1: Khung chu nhat huong len co dinh
    {1, 41, 0, 3, 24, 70, 11, 6, 10, 10, 0}, // Map 2: Tuong chan (rong 3px) + khung chu nhat
    {0,  0, 0, 0,  0, 70, 11, 4,  6, 36, 1}  // Map 3: Vong tron rong ruot di chuyen
};

MapConfig current_map;
volatile unsigned char digit[4];

// ============================================================================
// 4. KHAI BAO NGUYEN MAU HAM
// ============================================================================
void input_init(void);
void map_load(unsigned char id);
void physics_set_angle(int delta);
void physics_launch_ball(void);
void glcd_ui_init(void);
void glcd_render_frame(void);
void feedback_init(void);
void lcd_update_status(void);
void led7_set(unsigned char sc, unsigned char sh);
void timer0_ovf_init(void);
void rgb_set_state(unsigned char state);

// ============================================================================
// NHOM 1: KHOI TAO & DIEU PHOI HE THONG
// ============================================================================
#define RELAY_PULSE_TICKS 5
#define AUTO_SCAN_TICKS   5
#define AUTO_ANGLE_STEP   3
#define AUTO_FIRE_TICKS   35

static unsigned char auto_scan_cnt = 0;
static unsigned char auto_fire_cnt = 0;

void auto_reset_counters(void) {
    auto_scan_cnt = 0;
    auto_fire_cnt = 0;
}

void system_init(void) {
    #asm
        in   r30, 0x34
        ori  r30, 0x80
        out  0x34, r30
        out  0x34, r30
    #endasm

    DDRD.7 = 1;
    LCD_BL = 1;

    input_init();
    feedback_init();
    led7_set(0, 0);
    glcd_ui_init();

    timer0_ovf_init();
}

void game_reset(void) {
    btn_event = 0;
    key_event = 0;
    state_timer = 0;
    relay_timer = 0;
    RELAY1 = 0;
    auto_reset_counters();

    game_state = ST_AIM;
    auto_mode = 0;
    map_id = 1;
    last_result = 0;
    angle = 45;
    auto_angle_dir = 1;
    power_lvl = 2;
    score = 0;
    shots = 0;

    ball_x = (float)CANNON_X;
    ball_y = (float)CANNON_Y;
    vx = 0.0f;
    vy = 0.0f;

    map_load(1);
    led7_set(score, shots);
    rgb_set_state(0);

    lcd_dirty = 1;
    glcd_dirty = 1;
    lcd_update_status();
    glcd_render_frame();
}

void trigger_fire(void) {
    if (game_state != ST_AIM) return;

    if (shots < 99) {
        shots++;
    }
    led7_set(score, shots);

    RELAY1 = 1;
    relay_timer = RELAY_PULSE_TICKS;

    physics_launch_ball();

    last_result = 0;
    game_state = ST_FLY;
    auto_reset_counters();
    lcd_dirty = 1;
    glcd_dirty = 1;
}

void auto_step(void) {
    if (game_state != ST_AIM) return;

    if (++auto_scan_cnt >= AUTO_SCAN_TICKS) {
        auto_scan_cnt = 0;

        if (auto_angle_dir == 1) {
            physics_set_angle(AUTO_ANGLE_STEP);
            if (angle >= 75) {
                auto_angle_dir = -1;
            }
        } else {
            physics_set_angle(-AUTO_ANGLE_STEP);
            if (angle <= 15) {
                auto_angle_dir = 1;
            }
        }

        lcd_dirty = 1;
        glcd_dirty = 1;
    }

    if (++auto_fire_cnt >= AUTO_FIRE_TICKS) {
        auto_fire_cnt = 0;
        trigger_fire();
    }
}

// ============================================================================
// NHOM 2: GIAO TIEP DAU VAO (NUT BAM PB0, PB2, PB3 & MA TRAN PHIM)
// ============================================================================
#define INPUT_REPEAT_DELAY 20
#define INPUT_REPEAT_STEP   5

typedef struct {
    unsigned char previous;
    unsigned char stable;
} InputFilter;

static InputFilter button_filter, keypad_filter;
static unsigned char repeat_ticks;

static unsigned char press_event(InputFilter *filter, unsigned char sample) {
    if (sample == filter->previous && sample != filter->stable) {
        filter->stable = sample;
        return sample;
    }
    filter->previous = sample;
    return 0;
}

void input_init(void) {
    DDRB.2 = 0; DDRB.3 = 0; DDRB.0 = 0;
    PORTB.2 = 1; PORTB.3 = 1; PORTB.0 = 1;

    button_filter.previous = button_filter.stable = 0;
    keypad_filter.previous = keypad_filter.stable = 0;
    btn_event = key_event = repeat_ticks = 0;
}

// Quet ma tran 3x3 chuan board QG.20.07 (Cot 1: PF1, Cot 2: PF3, Cot 3: PF5)
unsigned char keypad_scan(void) {
    unsigned char saved_sreg, saved_pa7, i;
    unsigned int mask = 0;

    saved_sreg = SREG;
    #asm("cli")

    saved_pa7 = PORTA.7;
    PORTF   = 0x00;
    DDRF    = 0x00;
    PORTA.7 = 1;

    // Buoc 1: Keo Cot 1 (PF1) xuong 0 -> K0, K3, K6
    PORTF = (1 << 0) | (1 << 2) | (1 << 4);
    DDRF  = (1 << 1);
    delay_us(15);
    if (!(PINF & (1 << 2))) mask |= (1U << 0);
    if (!(PINF & (1 << 4))) mask |= (1U << 3);
    if (!(PINF & (1 << 0))) mask |= (1U << 6);
    DDRF  = 0x00;

    // Buoc 2: Keo Cot 2 (PF3) xuong 0 -> K1, K4, K7
    PORTF = (1 << 0) | (1 << 2) | (1 << 4);
    DDRF  = (1 << 3);
    delay_us(15);
    if (!(PINF & (1 << 2))) mask |= (1U << 1);
    if (!(PINF & (1 << 4))) mask |= (1U << 4);
    if (!(PINF & (1 << 0))) mask |= (1U << 7);
    DDRF  = 0x00;

    // Buoc 3: Keo Cot 3 (PF5) xuong 0 -> K2, K5, K8
    PORTF = (1 << 0) | (1 << 2) | (1 << 4);
    DDRF  = (1 << 5);
    delay_us(15);
    if (!(PINF & (1 << 2))) mask |= (1U << 2);
    if (!(PINF & (1 << 4))) mask |= (1U << 5);
    if (!(PINF & (1 << 0))) mask |= (1U << 8);
    DDRF  = 0x00;

    PORTF   = 0x00;
    DDRF    = 0xFF;
    PORTA.7 = saved_pa7;
    SREG    = saved_sreg;

    if (mask == 0 || (mask & (mask - 1)) != 0) return 0;

    for (i = 0; i < 9; ++i) {
        if (mask & (1U << i)) return (i + 1);
    }
    return 0;
}

void input_update(void) {
    unsigned char button = 0;

    if (!BT1)      button = 1;
    else if (!BT2) button = 2;
    else if (!BT3) button = 3;

    btn_event = press_event(&button_filter, button);

    if (btn_event == 2 || btn_event == 3) {
        repeat_ticks = INPUT_REPEAT_DELAY;
    } else if (button == button_filter.stable &&
               (button == 2 || button == 3)) {
        if (--repeat_ticks == 0) {
            btn_event = button;
            repeat_ticks = INPUT_REPEAT_STEP;
        }
    }

    key_event = press_event(&keypad_filter, keypad_scan());
}

void handle_keypad(void) {
    unsigned char key = key_event;
    key_event = 0;

    if (key == 9) {
        game_reset();
        return;
    }

    if (game_state != ST_AIM) return;

    switch (key) {
        case 1: case 2: case 3:
            power_lvl = key;
            break;

        case 4: case 5: case 6:
            map_load(key - 3);
            break;

        case 7:
            auto_mode = !auto_mode;
            auto_reset_counters();
            state_timer = 0;
            break;

        default:
            return;
    }

    lcd_dirty = glcd_dirty = 1;
}

// ============================================================================
// NHOM 3: TINH TOAN VAT LY & VA CHAM
// ============================================================================
#define PHYSICS_MIN_ANGLE_DEG        15
#define PHYSICS_MAX_ANGLE_DEG        75
#define TARGET_MOVE_INTERVAL_TICKS   3
#define PHYSICS_MAX_FLIGHT_TICKS     500
#define PHYSICS_SUBSTEPS_PER_TICK    4
#define PHYSICS_EPSILON              0.0001f
#define COLLISION_SEPARATION_PX      0.001f

#define COLLISION_NONE               0
#define COLLISION_HIT                1
#define COLLISION_MISS               2
#define COLLISION_BOUNCE             3

static int previous_target_y;
static unsigned char target_move_ticks = 0;
static unsigned char flight_result = COLLISION_NONE;
static unsigned int flight_ticks = 0;

void map_load(unsigned char id) {
    unsigned char i;

    if (id < 1 || id > 3) return;
    i = id - 1;

    current_map.has_wall     = MAPS[i].has_wall;
    current_map.wall_x       = MAPS[i].wall_x;
    current_map.wall_y       = MAPS[i].wall_y;
    current_map.wall_w       = MAPS[i].wall_w;
    current_map.wall_h       = MAPS[i].wall_h;
    current_map.basket_x     = MAPS[i].basket_x;
    current_map.basket_w     = MAPS[i].basket_w;
    current_map.basket_h     = MAPS[i].basket_h;
    current_map.basket_min_y = MAPS[i].basket_min_y;
    current_map.basket_max_y = MAPS[i].basket_max_y;
    current_map.is_moving    = MAPS[i].is_moving;

    map_id = id;
    target_y = current_map.basket_min_y;
    target_dir = 1;
    previous_target_y = target_y;
    target_move_ticks = 0;

    flight_result = COLLISION_NONE;
    flight_ticks = 0;
    ball_x = CANNON_X;
    ball_y = CANNON_Y;
    vx = 0.0f;
    vy = 0.0f;

    lcd_dirty = 1;
    glcd_dirty = 1;
}

void physics_set_angle(int delta) {
    long next_angle;

    next_angle = (long)angle + delta;
    if (next_angle < PHYSICS_MIN_ANGLE_DEG)
        next_angle = PHYSICS_MIN_ANGLE_DEG;
    if (next_angle > PHYSICS_MAX_ANGLE_DEG)
        next_angle = PHYSICS_MAX_ANGLE_DEG;

    if (angle != (int)next_angle) {
        angle = (int)next_angle;
        lcd_dirty = 1;
        glcd_dirty = 1;
    }
}

void physics_launch_ball(void) {
    float theta, speed, cosine, sine;

    physics_set_angle(0);
    if (power_lvl < 1 || power_lvl > 3) {
        power_lvl = 2;
        lcd_dirty = 1;
    }

    theta = angle * (3.14159265358979323846f / 180.0f);
    cosine = (float)cos(theta);
    sine = (float)sin(theta);
    speed = V0_TABLE[power_lvl];

    ball_x = CANNON_X + BARREL_LEN * cosine;
    ball_y = CANNON_Y + BARREL_LEN * sine;
    vx = speed * cosine;
    vy = speed * sine;

    previous_target_y = target_y;
    flight_result = COLLISION_NONE;
    flight_ticks = 0;
    glcd_dirty = 1;
}

void physics_update_target(void) {
    previous_target_y = target_y;

    if (!current_map.is_moving) return;
    if (++target_move_ticks < TARGET_MOVE_INTERVAL_TICKS) return;

    target_move_ticks = 0;
    target_y += target_dir;

    if (target_y >= current_map.basket_max_y) {
        target_y = current_map.basket_max_y;
        target_dir = -1;
    } else if (target_y <= current_map.basket_min_y) {
        target_y = current_map.basket_min_y;
        target_dir = 1;
    }

    glcd_dirty = 1;
}

static float abs_float(float x) {
    return x < 0.0f ? -x : x;
}

static void consider_collision(
    float fraction, unsigned char kind,
    signed char normal_x, signed char normal_y,
    float *best_fraction, unsigned char *best_kind,
    signed char *best_x, signed char *best_y)
{
    if (fraction < 0.0f || fraction > 1.0f) return;

    if (fraction < *best_fraction - PHYSICS_EPSILON) {
        *best_fraction = fraction;
        *best_kind = kind;
        *best_x = normal_x;
        *best_y = normal_y;
    } else if (abs_float(fraction - *best_fraction) <= PHYSICS_EPSILON &&
               kind == COLLISION_BOUNCE && *best_kind == COLLISION_BOUNCE) {
        if (normal_x != 0) *best_x = normal_x;
        if (normal_y != 0) *best_y = normal_y;
    }
}

static void physics_advance_substep(float duration, float tick_fraction) {
    float remaining, end_x, end_y, dx, dy, fraction, other;
    float best, consumed, left, right, bottom, top;
    float bx_left, bx_right, by_bot, by_top;
    float box_left, box_right, box_bottom, box_top;
    float target_cx, target_cy, hit_r, dist_x, dist_y;
    unsigned char kind, contacts;
    signed char normal_x, normal_y;

    remaining = duration;
    contacts = 0;

    while (remaining > PHYSICS_EPSILON && flight_result == COLLISION_NONE) {
        end_x = ball_x + vx * remaining;
        end_y = ball_y + vy * remaining - 0.5f * GRAVITY * remaining * remaining;
        dx = end_x - ball_x;
        dy = end_y - ball_y;

        best = 2.0f;
        kind = COLLISION_NONE;
        normal_x = 0;
        normal_y = 0;

        if (dx < -PHYSICS_EPSILON)
            consider_collision(-ball_x / dx, COLLISION_BOUNCE, 1, 0,
                &best, &kind, &normal_x, &normal_y);

        if (dx > PHYSICS_EPSILON)
            consider_collision((SCREEN_W - 1 - ball_x) / dx,
                COLLISION_BOUNCE, -1, 0, &best, &kind, &normal_x, &normal_y);

        if (dy > PHYSICS_EPSILON)
            consider_collision((SCREEN_H - 1 - ball_y) / dy,
                COLLISION_BOUNCE, 0, -1, &best, &kind, &normal_x, &normal_y);

        if (dy < -PHYSICS_EPSILON)
            consider_collision(-ball_y / dy, COLLISION_MISS, 0, 0,
                &best, &kind, &normal_x, &normal_y);

        if (current_map.has_wall) {
            left   = current_map.wall_x;
            right  = left + current_map.wall_w;
            bottom = current_map.wall_y;
            top    = bottom + current_map.wall_h;

            if (dx > PHYSICS_EPSILON) {
                fraction = (left - ball_x) / dx;
                other = ball_y + dy * fraction;
                if (other >= bottom && other <= top)
                    consider_collision(fraction, COLLISION_BOUNCE, -1, 0,
                        &best, &kind, &normal_x, &normal_y);
            }

            if (dx < -PHYSICS_EPSILON) {
                fraction = (right - ball_x) / dx;
                other = ball_y + dy * fraction;
                if (other >= bottom && other <= top)
                    consider_collision(fraction, COLLISION_BOUNCE, 1, 0,
                        &best, &kind, &normal_x, &normal_y);
            }

            if (dy < -PHYSICS_EPSILON) {
                fraction = (top - ball_y) / dy;
                other = ball_x + dx * fraction;
                if (other >= left && other <= right)
                    consider_collision(fraction, COLLISION_BOUNCE, 0, 1,
                        &best, &kind, &normal_x, &normal_y);
            }

            if (dy > PHYSICS_EPSILON) {
                fraction = (bottom - ball_y) / dy;
                other = ball_x + dx * fraction;
                if (other >= left && other <= right)
                    consider_collision(fraction, COLLISION_BOUNCE, 0, -1,
                        &best, &kind, &normal_x, &normal_y);
            }
        }

        if (!current_map.is_moving) {
            // Map 1 & Map 2: Khung chu nhat ho mieng tren (hinh chu U)
            bx_left  = (float)current_map.basket_x;
            bx_right = (float)((long)current_map.basket_x + current_map.basket_w - 1);
            by_bot   = (float)target_y;
            by_top   = (float)((long)target_y + current_map.basket_h);

            // 1. Va cham nay ra voi thanh ngoai va mep tren cua khung chu U
            if (dx > PHYSICS_EPSILON) {
                fraction = ((bx_left - 1.0f) - ball_x) / dx;
                other = ball_y + dy * fraction;
                if (other >= by_bot - 1.0f && other <= by_top + 0.5f)
                    consider_collision(fraction, COLLISION_BOUNCE, -1, 0,
                        &best, &kind, &normal_x, &normal_y);
            }

            if (dx < -PHYSICS_EPSILON) {
                fraction = ((bx_right + 1.0f) - ball_x) / dx;
                other = ball_y + dy * fraction;
                if (other >= by_bot - 1.0f && other <= by_top + 0.5f)
                    consider_collision(fraction, COLLISION_BOUNCE, 1, 0,
                        &best, &kind, &normal_x, &normal_y);
            }

            if (dy > PHYSICS_EPSILON) {
                fraction = ((by_bot - 1.0f) - ball_y) / dy;
                other = ball_x + dx * fraction;
                if (other >= bx_left - 1.0f && other <= bx_right + 1.0f)
                    consider_collision(fraction, COLLISION_BOUNCE, 0, -1,
                        &best, &kind, &normal_x, &normal_y);
            }

            if (dy < -PHYSICS_EPSILON) {
                fraction = ((by_top + 1.0f) - ball_y) / dy;
                other = ball_x + dx * fraction;
                if ((other >= bx_left - 1.0f && other <= bx_left + 1.0f) ||
                    (other >= bx_right - 1.0f && other <= bx_right + 1.0f)) {
                    consider_collision(fraction, COLLISION_BOUNCE, 0, 1,
                        &best, &kind, &normal_x, &normal_y);
                }
            }

            // 2. Chi tinh TRUNG khi dan lot qua mieng vao cham mep trong khung
            box_left   = bx_left + 1.5f;
            box_right  = bx_right - 1.6f;
            box_bottom = by_bot + 1.0f;
            box_top    = by_top - 1.0f;

            if (end_x >= box_left && end_x <= box_right &&
                end_y >= box_bottom && end_y <= box_top) {
                ball_x = end_x;
                ball_y = end_y;
                flight_result = COLLISION_HIT;
                return;
            }
        } else {
            // Map 3: Chi tinh TRUNG khi dan nam gon ben trong long vong tron
            target_cx = (float)((long)current_map.basket_x + (current_map.basket_w / 2));
            target_cy = (long)previous_target_y + (current_map.basket_h / 2)
                      + ((long)target_y - previous_target_y) * (tick_fraction + remaining);
            hit_r  = (float)(current_map.basket_w / 2) - 1.5f;
            dist_x = end_x - target_cx;
            dist_y = end_y - target_cy;

            if ((dist_x * dist_x + dist_y * dist_y) <= (hit_r * hit_r)) {
                ball_x = end_x;
                ball_y = end_y;
                flight_result = COLLISION_HIT;
                return;
            }
        }

        if (kind == COLLISION_NONE) {
            ball_x = end_x;
            ball_y = end_y;
            vy -= GRAVITY * remaining;
            return;
        }

        consumed = remaining * best;
        ball_x += dx * best;
        ball_y += dy * best;
        vy -= GRAVITY * consumed;
        tick_fraction += consumed;
        remaining -= consumed;

        if (kind == COLLISION_HIT || kind == COLLISION_MISS) {
            flight_result = kind;
            return;
        }

        if (normal_x != 0) {
            vx = -vx * BOUNCE_FACTOR;
            ball_x += normal_x * COLLISION_SEPARATION_PX;
        }
        if (normal_y != 0) {
            vy = -vy * BOUNCE_FACTOR;
            ball_y += normal_y * COLLISION_SEPARATION_PX;
        }

        if (++contacts >= 4) {
            flight_result = COLLISION_MISS;
            return;
        }
    }
}

void physics_update_ball(void) {
    unsigned char step;
    float duration;

    if (flight_result != COLLISION_NONE) return;

    duration = 1.0f / PHYSICS_SUBSTEPS_PER_TICK;
    for (step = 0;
         step < PHYSICS_SUBSTEPS_PER_TICK && flight_result == COLLISION_NONE;
         ++step) {
        physics_advance_substep(duration, step * duration);
    }

    if (++flight_ticks >= PHYSICS_MAX_FLIGHT_TICKS &&
        flight_result == COLLISION_NONE) {
        flight_result = COLLISION_MISS;
    }

    glcd_dirty = 1;
}

unsigned char physics_check_collision(void) {
    return flight_result;
}

// ============================================================================
// NHOM 4: DRIVER & DO HOA NOKIA 5110 TREN PORTG (84x48)
// ============================================================================
#define DEG_TO_RAD 0.01745329f

static unsigned char nokia_fb[504];

static void nokia_write_byte(unsigned char dat, unsigned char is_data) {
    unsigned char i;

    if (is_data) PORTG |=  (1 << NOKIA_DC_BIT);
    else         PORTG &= ~(1 << NOKIA_DC_BIT);

    PORTG &= ~(1 << NOKIA_CE_BIT);
    for (i = 0; i < 8; i++) {
        if (dat & 0x80) PORTG |=  (1 << NOKIA_DIN_BIT);
        else            PORTG &= ~(1 << NOKIA_DIN_BIT);

        PORTG |=  (1 << NOKIA_CLK_BIT);
        dat <<= 1;
        PORTG &= ~(1 << NOKIA_CLK_BIT);
    }
    PORTG |= (1 << NOKIA_CE_BIT);
}

static void nokia_flush(void) {
    unsigned int i;

    nokia_write_byte(0x80, 0);
    nokia_write_byte(0x40, 0);
    for (i = 0; i < 504; i++) {
        nokia_write_byte(nokia_fb[i], 1);
    }
}

static void fb_clear(void) {
    unsigned int i;
    for (i = 0; i < 504; i++) nokia_fb[i] = 0x00;
}

static void fb_setpixel(int x, int y) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    nokia_fb[(y >> 3) * SCREEN_W + x] |= (1 << (y & 7));
}

static void fb_line(int x0, int y0, int x1, int y1) {
    int dx, sx, dy, sy, err, e2;

    dx  = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    sx  = (x0 < x1) ? 1 : -1;
    dy  = (y1 > y0) ? (y0 - y1) : (y1 - y0);
    sy  = (y0 < y1) ? 1 : -1;
    err = dx + dy;

    while (1) {
        fb_setpixel(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void fb_bar(int x0, int y0, int x1, int y1) {
    int x, y;

    if (x0 > x1) { x = x0; x0 = x1; x1 = x; }
    if (y0 > y1) { y = y0; y0 = y1; y1 = y; }
    for (y = y0; y <= y1; y++) {
        for (x = x0; x <= x1; x++) fb_setpixel(x, y);
    }
}

static void fb_circle(int cx, int cy, int r) {
    int x, y, err;

    x = r;
    y = 0;
    err = 1 - x;
    while (x >= y) {
        fb_setpixel(cx + x, cy + y); fb_setpixel(cx + y, cy + x);
        fb_setpixel(cx - y, cy + x); fb_setpixel(cx - x, cy + y);
        fb_setpixel(cx - x, cy - y); fb_setpixel(cx - y, cy - x);
        fb_setpixel(cx + y, cy - x); fb_setpixel(cx + x, cy - y);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

static void fb_fillcircle(int cx, int cy, int r) {
    int x, y;

    for (y = -r; y <= r; y++) {
        for (x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) fb_setpixel(cx + x, cy + y);
        }
    }
}

static int phys_to_glcd_y(int py) {
    int gy = (SCREEN_H - 1) - py;
    if (gy < 0) gy = 0;
    if (gy >= SCREEN_H) gy = SCREEN_H - 1;
    return gy;
}

static int clamp_glcd_x(int gx) {
    if (gx < 0) return 0;
    if (gx >= SCREEN_W) return SCREEN_W - 1;
    return gx;
}

void glcd_ui_init(void) {
    DDRG  |= (1 << NOKIA_DC_BIT) | (1 << NOKIA_DIN_BIT) | (1 << NOKIA_CLK_BIT)
           | (1 << NOKIA_RST_BIT) | (1 << NOKIA_CE_BIT);
    PORTG |= (1 << NOKIA_RST_BIT) | (1 << NOKIA_CE_BIT);
    PORTG &= ~(1 << NOKIA_CLK_BIT);

    LCD_BL = 1;

    PORTG &= ~(1 << NOKIA_RST_BIT);
    delay_ms(10);
    PORTG |=  (1 << NOKIA_RST_BIT);

    nokia_write_byte(0x21, 0);
    nokia_write_byte(0xC2, 0);
    nokia_write_byte(0x04, 0);
    nokia_write_byte(0x14, 0);
    nokia_write_byte(0x20, 0);
    nokia_write_byte(0x0C, 0);

    fb_clear();
    nokia_flush();
    glcd_dirty = 1;
}

static void glcd_draw_cannon(void) {
    float rad;
    int base_gx, base_gy, tip_gx, tip_gy, dot_x, dot_y;

    rad     = (float)angle * DEG_TO_RAD;
    base_gx = CANNON_X;
    base_gy = phys_to_glcd_y(CANNON_Y);
    tip_gx  = clamp_glcd_x((int)(CANNON_X + BARREL_LEN * cos(rad) + 0.5f));
    tip_gy  = phys_to_glcd_y((int)(CANNON_Y + BARREL_LEN * sin(rad) + 0.5f));

    fb_fillcircle(base_gx, base_gy, 4);
    fb_bar(1, base_gy + 1, 11, SCREEN_H - 1);

    fb_line(base_gx, base_gy, tip_gx, tip_gy);
    if (base_gy > 0 && tip_gy > 0) {
        fb_line(base_gx, base_gy - 1, tip_gx, tip_gy - 1);
    }

    if (game_state == ST_AIM) {
        dot_x = (int)(CANNON_X + (BARREL_LEN + 4) * cos(rad) + 0.5f);
        dot_y = (SCREEN_H - 1) - (int)(CANNON_Y + (BARREL_LEN + 4) * sin(rad) + 0.5f);
        fb_setpixel(dot_x, dot_y);

        dot_x = (int)(CANNON_X + (BARREL_LEN + 8) * cos(rad) + 0.5f);
        dot_y = (SCREEN_H - 1) - (int)(CANNON_Y + (BARREL_LEN + 8) * sin(rad) + 0.5f);
        fb_setpixel(dot_x, dot_y);
    }
}

static void glcd_draw_wall(unsigned char idx) {
    int wx0, wx1, wy0, wy1;

    if (!MAPS[idx].has_wall) return;

    wx0 = clamp_glcd_x(MAPS[idx].wall_x);
    wx1 = clamp_glcd_x(MAPS[idx].wall_x + MAPS[idx].wall_w - 1);
    wy1 = phys_to_glcd_y(MAPS[idx].wall_y);
    wy0 = phys_to_glcd_y(MAPS[idx].wall_y + MAPS[idx].wall_h - 1);

    fb_bar(wx0, wy0, wx1, wy1);
}

static void glcd_draw_target(unsigned char idx) {
    int bx0, bx1, by_top, by_bot;
    int cx, cy, r;

    if (!MAPS[idx].is_moving) {
        // Map 1 & Map 2: 1 khung chu nhat ho mieng huong len (hinh chu U)
        bx0    = clamp_glcd_x(MAPS[idx].basket_x);
        bx1    = clamp_glcd_x(MAPS[idx].basket_x + MAPS[idx].basket_w - 1);
        by_bot = phys_to_glcd_y(target_y);
        by_top = phys_to_glcd_y(target_y + MAPS[idx].basket_h);

        fb_line(bx0, by_top, bx0, by_bot); // Thanh ben trai
        fb_line(bx0, by_bot, bx1, by_bot); // Day khung chu nhat
        fb_line(bx1, by_top, bx1, by_bot); // Thanh ben phai
    } else {
        // Map 3: Vong tron rong ruot di chuyen len xuong
        r = MAPS[idx].basket_w / 2;
        if (r < 2) r = 2;

        cx = clamp_glcd_x(MAPS[idx].basket_x + r);
        cy = phys_to_glcd_y(target_y + (MAPS[idx].basket_h / 2));

        if (cx < r) cx = r;
        if (cx >= SCREEN_W - r) cx = SCREEN_W - 1 - r;
        if (cy < r) cy = r;
        if (cy >= SCREEN_H - r) cy = SCREEN_H - 1 - r;

        fb_circle(cx, cy, r);
    }
}

static void glcd_draw_ball(void) {
    int gx, gy;
    int size, half, x0, y0;

    if (game_state != ST_FLY && game_state != ST_RESULT) return;

    // Hinh vuong co canh bang chieu rong vat can Map 2 (MAPS[1].wall_w = 3 pixel)
    size = MAPS[1].wall_w;
    if (size < 1) size = 1;
    half = size / 2;

    gx = (int)(ball_x + 0.5f);
    gy = (SCREEN_H - 1) - (int)(ball_y + 0.5f);

    if (gx >= 0 && gx < SCREEN_W && gy >= 0 && gy < SCREEN_H) {
        x0 = gx - half;
        y0 = gy - half;
        if (x0 < 0) x0 = 0;
        if (x0 > SCREEN_W - size) x0 = SCREEN_W - size;
        if (y0 < 0) y0 = 0;
        if (y0 > SCREEN_H - size) y0 = SCREEN_H - size;

        fb_bar(x0, y0, x0 + size - 1, y0 + size - 1);
    }
}

void glcd_render_frame(void) {
    unsigned char idx;

    idx = (map_id >= 1 && map_id <= 3) ? (map_id - 1) : 0;

    fb_clear();
    fb_line(0, SCREEN_H - 1, SCREEN_W - 1, SCREEN_H - 1);
    glcd_draw_wall(idx);
    glcd_draw_target(idx);
    glcd_draw_cannon();
    glcd_draw_ball();

    nokia_flush();
    glcd_dirty = 0;
}

// ============================================================================
// NHOM 5: HIEN THI PHU & TIN HIEU BAO HIEU
// ============================================================================
void feedback_init(void) {
    lcd_init(16);
    lcd_clear();
    lcd_gotoxy(0, 0);

    PORTF = 0x00;
    DDRF  = 0xFF;

    DDRD.3 = 1; DDRA.7 = 1; DDRD.1 = 1; DDRD.2 = 1;
    PORTD.3 = 1; PORTA.7 = 1; PORTD.1 = 1; PORTD.2 = 1;

    DDRD.4 = 1; DDRD.5 = 1; DDRD.6 = 1;
    PORTD.4 = 0; PORTD.5 = 0; PORTD.6 = 0;

    DDRC.3 = 1;
    PORTC.3 = 0;
}

void lcd_update_status(void) {
    char mode;

    if (lcd_dirty) {
        mode = (auto_mode == 1) ? 'A' : 'M';

        lcd_gotoxy(0, 0);
        sprintf(lcd_buf, "M:%c A:%-2d Map:%d  ", mode, angle, map_id);
        lcd_puts(lcd_buf);

        lcd_gotoxy(0, 1);
        if (last_result == 1) {
            sprintf(lcd_buf2, "Luc:%d TRUNG     ", power_lvl);
        } else if (last_result == 2) {
            sprintf(lcd_buf2, "Luc:%d TRUOT     ", power_lvl);
        } else {
            sprintf(lcd_buf2, "Luc:%d CHO       ", power_lvl);
        }
        lcd_puts(lcd_buf2);

        lcd_dirty = 0;
    }
}

flash unsigned char seg7[10] = {0xFC, 0x60, 0xDA, 0xF2, 0x66, 0xB6, 0xBE, 0xE0, 0xFE, 0xF6};

// Quet LED 7 doan chuan board QG.20.07: 2 den trai = Diem | 2 den phai = Luot ban
void led7_scan_once(unsigned char pos) {
    unsigned char s, pf;

    PORTF = 0x00; // Tat digit cu truoc khi doi thanh LED de tranh lem net

    s  = seg7[digit[pos]];
    pf = 0x00;

    if (!(s & 0x80)) pf |= (1U << 1); // a  -> PF1
    if (!(s & 0x40)) pf |= (1U << 5); // b  -> PF5
    if (!(s & 0x04)) pf |= (1U << 2); // f  -> PF2
    if (!(s & 0x01)) pf |= (1U << 7); // dp -> PF7

    PORTA.7 = (s & 0x20) ? 0 : 1;     // c  -> PA7
    PORTD.1 = (s & 0x10) ? 0 : 1;     // d  -> PD1
    PORTD.2 = (s & 0x08) ? 0 : 1;     // e  -> PD2
    PORTD.3 = (s & 0x02) ? 0 : 1;     // g  -> PD3

    switch (pos) {
        case 0: pf |= (1U << 0); break; // LED1 -> PF0
        case 1: pf |= (1U << 3); break; // LED2 -> PF3
        case 2: pf |= (1U << 4); break; // LED3 -> PF4
        case 3: pf |= (1U << 6); break; // LED4 -> PF6
    }

    PORTF = pf;
}

void led7_set(unsigned char sc, unsigned char sh) {
    if (sc > 99) sc = 99;
    if (sh > 99) sh = 99;
    digit[0] = sc / 10; digit[1] = sc % 10;
    digit[2] = sh / 10; digit[3] = sh % 10;
}

// ============================================================================
// NGAT TIMER0 (2ms QUET 1 LED 7 DOAN -> 125Hz, 20ms TAO NHIP GAME)
// ============================================================================
volatile unsigned char tick_flag = 0;

void timer0_ovf_init(void) {
    TCCR0 = (1 << CS02);   // Bo chia 64 (o 8MHz: 1 tick = 8us)
    TCNT0 = 0x06;          // 250 * 8us = 2ms moi lan quet 1 so
    TIMSK |= (1 << TOIE0);
    #asm("sei")
}

interrupt [TIM0_OVF] void timer0_ovf_isr(void) {
    static unsigned char digit_idx = 0;
    static unsigned char sub_tick  = 0;

    TCNT0 = 0x06;

    led7_scan_once(digit_idx);
    digit_idx = (digit_idx + 1) & 3;

    if (++sub_tick >= 10) { // 10 * 2ms = 20ms nhip game
        sub_tick  = 0;
        tick_flag = 1;
    }
}

void rgb_set_state(unsigned char state) {
    LED_R = 0; LED_G = 0; LED_B = 0;

    if (state == 0)      LED_B = 1;
    else if (state == 1) LED_G = 1;
    else if (state == 2) LED_R = 1;
}

// ============================================================================
// 6. HAM MAIN
// ============================================================================
void main(void) {
    unsigned char saved_sreg, have_tick;
    unsigned char result, reset_requested;
    unsigned char glcd_skip = 0;

    system_init();
    game_reset();
    led7_set(score, shots);

    while (1) {
        saved_sreg = SREG;
        #asm("cli")
        have_tick = tick_flag;
        tick_flag = 0;
        SREG = saved_sreg;

        if (!have_tick) continue;

        if (state_timer != 0) --state_timer;

        if (relay_timer != 0) {
            --relay_timer;
            if (relay_timer == 0) RELAY1 = 0;
        }

        input_update();
        reset_requested = (key_event == 9);
        if (reset_requested) btn_event = 0;
        if (game_state == ST_AIM || reset_requested) handle_keypad();
        key_event = 0;

        if (!reset_requested) {
            switch (game_state) {
                case ST_AIM:
                    physics_update_target();

                    if (auto_mode) {
                        auto_step();
                    } else {
                        switch (btn_event) {
                            case 1: trigger_fire(); break;
                            case 2: physics_set_angle(1); break;
                            case 3: physics_set_angle(-1); break;
                        }
                    }

                    if (game_state == ST_FLY) {
                        lcd_dirty = 1;
                        glcd_dirty = 1;
                    }
                    break;

                case ST_FLY:
                    physics_update_target();
                    physics_update_ball();
                    result = physics_check_collision();

                    if (result != COLLISION_NONE) {
                        if (result == COLLISION_HIT) {
                            if (score + map_id <= 99) score += map_id;
                            else score = 99;
                        }

                        last_result = result;
                        rgb_set_state(result);
                        state_timer = 50;
                        game_state = ST_RESULT;
                        lcd_dirty = 1;
                        glcd_dirty = 1;
                    }
                    break;

                case ST_RESULT:
                    if (state_timer == 0) {
                        game_state = ST_AIM;
                        last_result = 0;
                        rgb_set_state(0);
                        lcd_dirty = 1;
                        glcd_dirty = 1;
                    }
                    break;

                default:
                    game_reset();
                    break;
            }
        }

        btn_event = 0;
        led7_set(score, shots);

        lcd_update_status();
        if (glcd_dirty) {
            if (game_state == ST_FLY) {
                if (++glcd_skip >= 2) {
                    glcd_skip = 0;
                    glcd_render_frame();
                }
            } else {
                glcd_skip = 0;
                glcd_render_frame();
            }
        }
    }
}
