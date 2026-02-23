/* Copyright 2026
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "app/snake.h"

#ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
#include "screenshot.h"
#endif

#define SNAKE_CELL_SIZE_SMALL     4
#define SNAKE_CELL_SIZE_LARGE     8
#define SNAKE_CELL_SIZE_DEFAULT   SNAKE_CELL_SIZE_SMALL
#define SNAKE_MAX_GRID_W          (LCD_WIDTH / SNAKE_CELL_SIZE_SMALL)
#define SNAKE_MAX_GRID_H          ((LCD_HEIGHT - 8) / SNAKE_CELL_SIZE_SMALL)
#define SNAKE_MAX_LEN             (SNAKE_MAX_GRID_W * SNAKE_MAX_GRID_H)
#define SNAKE_START_LEN           5
#define SNAKE_MAX_OBSTACLES       64
#define SNAKE_MAX_FOOD            5
#define SNAKE_MAX_LEVEL           7
#define SNAKE_FOOD_MOVE_TICKS_SLOW 8
#define SNAKE_FOOD_MOVE_TICKS_FAST 3

static uint32_t randSeed = 1;

static bool isInitialized = false;
static bool isPaused = false;
static bool isGameOver = false;
static bool isBeep = false;

typedef struct {
    uint8_t cell_size;
    uint8_t level_setting; // 0 = auto, 1..SNAKE_MAX_LEVEL = fixed
} SnakeSettings;

static SnakeSettings gSnakeSettings = {SNAKE_CELL_SIZE_DEFAULT, 0};

static const uint16_t gSnakeBaseDelay[SNAKE_MAX_LEVEL] = {150, 150, 150, 150, 150, 150, 150};
static const uint16_t gSnakeMinDelay[SNAKE_MAX_LEVEL]  = {90,  80,  70,  60,  55,  50,  45};
static const uint8_t gSnakeAccel[SNAKE_MAX_LEVEL]      = {2,   3,   4,   5,   6,   7,   8};
static const uint16_t gSnakeLevelScore[6]              = {10, 25, 50, 80, 110, 140};

static uint16_t score = 0;
static uint16_t snake_len = 0;
static uint8_t snake_level = 1;

static int8_t dir_x = 1;
static int8_t dir_y = 0;

static uint8_t snake_x[SNAKE_MAX_LEN];
static uint8_t snake_y[SNAKE_MAX_LEN];
static uint8_t food_x[SNAKE_MAX_FOOD];
static uint8_t food_y[SNAKE_MAX_FOOD];
static uint8_t food_count = 1;
static uint8_t food_move_tick = 0;
static uint8_t obstacle_x[SNAKE_MAX_OBSTACLES];
static uint8_t obstacle_y[SNAKE_MAX_OBSTACLES];
static uint8_t obstacle_count = 0;

static uint16_t tone = 0;

static char str[16];

static KeyboardState kbd = {KEY_INVALID, KEY_INVALID, 0};

static KEY_Code_t GetKey(void);

static uint8_t getLevelFromScore(uint16_t value)
{
    for (uint8_t i = 0; i < ARRAY_SIZE(gSnakeLevelScore); i++) {
        if (value < gSnakeLevelScore[i]) {
            return (uint8_t)(i + 1);
        }
    }
    return SNAKE_MAX_LEVEL;
}

static uint8_t snakeGridW(void)
{
    return (uint8_t)(LCD_WIDTH / gSnakeSettings.cell_size);
}

static uint8_t snakeGridH(void)
{
    return (uint8_t)((LCD_HEIGHT - 8) / gSnakeSettings.cell_size);
}

// Initialise seed
static void srand_custom(uint32_t seed) {
    randSeed = seed;
}

// Return pseudo-random from 0 to RAND_MAX (here 32767)
static int rand_custom(void) {
    randSeed = randSeed * 1103515245 + 12345;
    return (randSeed >> 16) & 0x7FFF; // 15 bits
}

// Return integer from min to max include
static uint8_t randInt(uint8_t min, uint8_t max) {
    return (uint8_t)(min + (rand_custom() % (max - min + 1)));
}

static void playBeep(uint16_t tone)
{
    BK4819_PlayTone(tone, true);
    AUDIO_AudioPathOn();
    BK4819_ExitTxMute();
    SYSTEM_DelayMs(100);
    BK4819_EnterTxMute();
    AUDIO_AudioPathOff();
}

static void drawScore(void)
{
    memset(gStatusLine, 0, sizeof(gStatusLine));

    sprintf(str, "SCORE %03u", score);
    GUI_DisplaySmallest(str, 0, 1, true, true);

    sprintf(str, "L%u", snake_level);
    GUI_DisplaySmallest(str, 46, 1, true, true);

    sprintf(str, "LEN %03u", snake_len);
    GUI_DisplaySmallest(str, 72, 1, true, true);
}

static void drawCell(uint8_t gx, uint8_t gy, bool on)
{
    uint8_t size = gSnakeSettings.cell_size;
    uint8_t x0 = gx * size;
    uint8_t y0 = gy * size;

    for (uint8_t y = 0; y < size; y++) {
        UI_DrawLineBuffer(gFrameBuffer, x0, y0 + y, x0 + size - 1, y0 + y, on);
    }
}

static void drawFood(uint8_t gx, uint8_t gy, bool on)
{
    uint8_t size = gSnakeSettings.cell_size;
    uint8_t dot = (size >= SNAKE_CELL_SIZE_LARGE) ? 3 : 2;
    uint8_t x0 = gx * size + (size / 2) - (dot / 2);
    uint8_t y0 = gy * size + (size / 2) - (dot / 2);

    for (uint8_t y = 0; y < dot; y++) {
        for (uint8_t x = 0; x < dot; x++) {
            UI_DrawPixelBuffer(gFrameBuffer, x0 + x, y0 + y, on);
        }
    }
}

static void redrawPlayfield(void)
{
    UI_DisplayClear();

    for (uint8_t i = 0; i < obstacle_count; i++) {
        drawCell(obstacle_x[i], obstacle_y[i], true);
    }

    for (uint16_t i = 0; i < snake_len; i++) {
        drawCell(snake_x[i], snake_y[i], true);
    }

    for (uint8_t i = 0; i < food_count; i++) {
        if (food_x[i] != 0xFF) {
            drawFood(food_x[i], food_y[i], true);
        }
    }
}

static bool cellOnSnake(uint8_t gx, uint8_t gy, uint16_t limit)
{
    for (uint16_t i = 0; i < limit; i++) {
        if (snake_x[i] == gx && snake_y[i] == gy) {
            return true;
        }
    }
    return false;
}

static bool cellOnObstacle(uint8_t gx, uint8_t gy)
{
    for (uint8_t i = 0; i < obstacle_count; i++) {
        if (obstacle_x[i] == gx && obstacle_y[i] == gy) {
            return true;
        }
    }
    return false;
}

static bool cellOnFood(uint8_t gx, uint8_t gy, int8_t ignore)
{
    for (uint8_t i = 0; i < food_count; i++) {
        if ((int8_t)i == ignore) {
            continue;
        }
        if (food_x[i] != 0xFF && food_x[i] == gx && food_y[i] == gy) {
            return true;
        }
    }
    return false;
}

static int8_t foodIndexAt(uint8_t gx, uint8_t gy)
{
    for (uint8_t i = 0; i < food_count; i++) {
        if (food_x[i] != 0xFF && food_x[i] == gx && food_y[i] == gy) {
            return (int8_t)i;
        }
    }
    return -1;
}

static void addObstacle(uint8_t gx, uint8_t gy)
{
    if (obstacle_count >= SNAKE_MAX_OBSTACLES) {
        return;
    }

    if (cellOnObstacle(gx, gy)) {
        return;
    }
    if (cellOnSnake(gx, gy, snake_len)) {
        return;
    }
    if (cellOnFood(gx, gy, -1)) {
        return;
    }

    obstacle_x[obstacle_count] = gx;
    obstacle_y[obstacle_count] = gy;
    obstacle_count++;
}

static void buildObstacles(void)
{
    obstacle_count = 0;

    if (snake_level < 2) {
        return;
    }

    uint8_t w = snakeGridW();
    uint8_t h = snakeGridH();
    uint8_t cx = w / 2;
    uint8_t cy = h / 2;

    uint8_t row = (cy > 1) ? (uint8_t)(cy - 1) : (uint8_t)(cy + 1);
    for (uint8_t x = 1; x + 1 < w; x++) {
        if (x >= (uint8_t)(cx - 1) && x <= (uint8_t)(cx + 1)) {
            continue;
        }
        addObstacle(x, row);
    }

    if (snake_level >= 3) {
        uint8_t col = (uint8_t)(cx + 1);
        if (col >= w - 1) {
            col = (uint8_t)(cx - 1);
        }

        for (uint8_t y = 1; y + 1 < h; y++) {
            if (y >= (uint8_t)(cy - 1) && y <= (uint8_t)(cy + 1)) {
                continue;
            }
            addObstacle(col, y);
        }
    }

    for (uint8_t i = 0; i < obstacle_count; i++) {
        drawCell(obstacle_x[i], obstacle_y[i], true);
    }
}

static uint8_t foodCountForLevel(uint8_t level)
{
    if (level >= 5 && level <= 6) {
        return SNAKE_MAX_FOOD;
    }
    return 1;
}

static uint8_t foodMoveTicksForLevel(uint8_t level)
{
    if (level >= 7) {
        return SNAKE_FOOD_MOVE_TICKS_FAST;
    }
    if (level >= 4) {
        return SNAKE_FOOD_MOVE_TICKS_SLOW;
    }
    return 0;
}

static void clearFood(void)
{
    for (uint8_t i = 0; i < SNAKE_MAX_FOOD; i++) {
        food_x[i] = 0xFF;
        food_y[i] = 0xFF;
    }
}

static void placeFoodAt(uint8_t index)
{
    if (index >= food_count) {
        return;
    }
    if (snake_len >= SNAKE_MAX_LEN) {
        return;
    }

    for (uint16_t tries = 0; tries < 200; tries++) {
        uint8_t gx = randInt(0, snakeGridW() - 1);
        uint8_t gy = randInt(0, snakeGridH() - 1);
        if (cellOnSnake(gx, gy, snake_len) || cellOnObstacle(gx, gy) || cellOnFood(gx, gy, index)) {
            continue;
        }
        food_x[index] = gx;
        food_y[index] = gy;
        drawFood(gx, gy, true);
        return;
    }
}

static void placeFoodAll(void)
{
    food_count = foodCountForLevel(snake_level);
    clearFood();
    for (uint8_t i = 0; i < food_count; i++) {
        placeFoodAt(i);
    }
    food_move_tick = 0;
}

static void moveFood(void)
{
    uint8_t ticks = foodMoveTicksForLevel(snake_level);
    if (ticks == 0 || food_count == 0) {
        return;
    }

    if (++food_move_tick < ticks) {
        return;
    }
    food_move_tick = 0;

    for (uint8_t i = 0; i < food_count; i++) {
        if (food_x[i] == 0xFF) {
            continue;
        }

        int8_t dir = (int8_t)(randInt(0, 3));
        int8_t dx = 0;
        int8_t dy = 0;

        switch (dir) {
        case 0: dx = 1; break;
        case 1: dx = -1; break;
        case 2: dy = 1; break;
        default: dy = -1; break;
        }

        int16_t nx = (int16_t)food_x[i] + dx;
        int16_t ny = (int16_t)food_y[i] + dy;

        if (snake_level >= 3) {
            if (nx < 0) {
                nx = snakeGridW() - 1;
            } else if (nx >= snakeGridW()) {
                nx = 0;
            }
        }

        if (nx < 0 || nx >= snakeGridW() || ny < 0 || ny >= snakeGridH()) {
            continue;
        }

        if (cellOnObstacle((uint8_t)nx, (uint8_t)ny) || cellOnSnake((uint8_t)nx, (uint8_t)ny, snake_len) || cellOnFood((uint8_t)nx, (uint8_t)ny, (int8_t)i)) {
            continue;
        }

        drawFood(food_x[i], food_y[i], false);
        food_x[i] = (uint8_t)nx;
        food_y[i] = (uint8_t)ny;
        drawFood(food_x[i], food_y[i], true);
    }
}

static void resetGame(void)
{
    UI_DisplayClear();
    memset(gStatusLine, 0, sizeof(gStatusLine));

    score = 0;
    snake_len = SNAKE_START_LEN;
    snake_level = (gSnakeSettings.level_setting == 0) ? 1 : gSnakeSettings.level_setting;
    dir_x = 1;
    dir_y = 0;
    food_move_tick = 0;

    uint8_t start_x = snakeGridW() / 2;
    uint8_t start_y = snakeGridH() / 2;

    food_count = 0;
    clearFood();
    buildObstacles();

    for (uint16_t i = 0; i < snake_len; i++) {
        snake_x[i] = start_x - i;
        snake_y[i] = start_y;
        drawCell(snake_x[i], snake_y[i], true);
    }

    placeFoodAll();

    isPaused = false;
    isGameOver = false;
}

static void setDirection(int8_t dx, int8_t dy)
{
    if (dx == -dir_x && dy == -dir_y) {
        return; // no reverse
    }

    dir_x = dx;
    dir_y = dy;
}

static bool stepSnake(void)
{
    int16_t next_x = (int16_t)snake_x[0] + dir_x;
    int16_t next_y = (int16_t)snake_y[0] + dir_y;

    if (snake_level >= 3) {
        if (next_x < 0) {
            next_x = snakeGridW() - 1;
        } else if (next_x >= snakeGridW()) {
            next_x = 0;
        }
    }

    if (next_x < 0 || next_x >= snakeGridW() || next_y < 0 || next_y >= snakeGridH()) {
        return false;
    }

    if (cellOnObstacle((uint8_t)next_x, (uint8_t)next_y)) {
        return false;
    }

    int8_t foodIndex = foodIndexAt((uint8_t)next_x, (uint8_t)next_y);
    bool grow = (foodIndex >= 0);
    if (grow && snake_len >= SNAKE_MAX_LEN) {
        grow = false;
    }
    uint16_t limit = grow ? snake_len : (snake_len > 0 ? snake_len - 1 : 0);

    if (cellOnSnake((uint8_t)next_x, (uint8_t)next_y, limit)) {
        return false;
    }

    if (!grow && snake_len > 0) {
        drawCell(snake_x[snake_len - 1], snake_y[snake_len - 1], false);
    }

    if (grow) {
        snake_len++;
    }

    for (uint16_t i = snake_len - 1; i > 0; i--) {
        snake_x[i] = snake_x[i - 1];
        snake_y[i] = snake_y[i - 1];
    }

    snake_x[0] = (uint8_t)next_x;
    snake_y[0] = (uint8_t)next_y;

    drawCell(snake_x[0], snake_y[0], true);

    if (grow) {
        score++;
        placeFoodAt((uint8_t)foodIndex);
        isBeep = true;
        tone = 600;
    }

    return true;
}

static void showPause(bool show)
{
    if (show) {
        UI_PrintStringSmallBold("PAUSE", 0, 128, 4);
        return;
    }

    for (uint8_t i = 0; i < 8; i++) {
        UI_DrawLineBuffer(gFrameBuffer, 32, 32 + i, 96, 32 + i, false);
    }
}

static void showGameOver(void)
{
    UI_PrintStringSmallBold("GAME", 32, 0, 4);
    UI_PrintStringSmallBold("OVER", 66, 0, 4);
}

static void DrawSnakeMenu(uint8_t selection)
{
    UI_DisplayClear();
    memset(gStatusLine, 0, sizeof(gStatusLine));

    UI_PrintStringSmallBold("SNAKE", 44, 0, 1);

    if (selection == 0) {
        UI_PrintStringSmallBoldInverse("START", 36, 0, 3);
        UI_PrintStringSmallBold("CONFIG", 34, 0, 5);
    } else {
        UI_PrintStringSmallBold("START", 36, 0, 3);
        UI_PrintStringSmallBoldInverse("CONFIG", 34, 0, 5);
    }

    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}

static void DrawConfigMenu(uint8_t selection)
{
    const char *sizeLabel = (gSnakeSettings.cell_size == SNAKE_CELL_SIZE_LARGE) ? "8PX" : "4PX";
    char levelLabel[8];

    UI_DisplayClear();
    memset(gStatusLine, 0, sizeof(gStatusLine));

    UI_PrintStringSmallBold("CONFIG", 40, 0, 1);

    if (selection == 0) {
        UI_PrintStringSmallBoldInverse("SIZE", 8, 0, 3);
    } else {
        UI_PrintStringSmallBold("SIZE", 8, 0, 3);
    }
    UI_PrintStringSmallBold(sizeLabel, 72, 0, 3);

    if (selection == 1) {
        UI_PrintStringSmallBoldInverse("LEVEL", 8, 0, 5);
    } else {
        UI_PrintStringSmallBold("LEVEL", 8, 0, 5);
    }
    if (gSnakeSettings.level_setting == 0) {
        strcpy(levelLabel, "AUTO");
    } else {
        sprintf(levelLabel, "L%u", gSnakeSettings.level_setting);
    }
    UI_PrintStringSmallBold(levelLabel, 72, 0, 5);

    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}

static KEY_Code_t ReadKeyOnce(void)
{
    kbd.prev = kbd.current;
    kbd.current = GetKey();

    if (kbd.current == KEY_INVALID || kbd.current == kbd.prev) {
        return KEY_INVALID;
    }

    return kbd.current;
}

static void RunConfigMenu(void)
{
    uint8_t selection = 0;

    DrawConfigMenu(selection);

    while (1) {
        KEY_Code_t key = ReadKeyOnce();

        if (key == KEY_INVALID) {
            SYSTEM_DelayMs(30);
            continue;
        }

        switch (key) {
        case KEY_UP:
        case KEY_2:
        case KEY_DOWN:
        case KEY_8:
            selection = (selection == 0) ? 1 : 0;
            DrawConfigMenu(selection);
            break;
        case KEY_4:
            if (selection == 0) {
                gSnakeSettings.cell_size = (gSnakeSettings.cell_size == SNAKE_CELL_SIZE_SMALL) ? SNAKE_CELL_SIZE_LARGE : SNAKE_CELL_SIZE_SMALL;
            } else {
                if (gSnakeSettings.level_setting == 0) {
                    gSnakeSettings.level_setting = SNAKE_MAX_LEVEL;
                } else {
                    gSnakeSettings.level_setting--;
                }
            }
            DrawConfigMenu(selection);
            break;
        case KEY_6:
        case KEY_0:
            if (selection == 0) {
                gSnakeSettings.cell_size = (gSnakeSettings.cell_size == SNAKE_CELL_SIZE_SMALL) ? SNAKE_CELL_SIZE_LARGE : SNAKE_CELL_SIZE_SMALL;
            } else {
                gSnakeSettings.level_setting = (uint8_t)((gSnakeSettings.level_setting + 1) % (SNAKE_MAX_LEVEL + 1));
            }
            DrawConfigMenu(selection);
            break;
        case KEY_MENU:
        case KEY_EXIT:
            return;
        default:
            break;
        }
    }
}

static void OnKeyDown(uint8_t key)
{
    bool wasPaused = isPaused;

    switch (key) {
    case KEY_2:
    case KEY_UP:
        if (!isPaused) {
            setDirection(0, -1);
        }
        break;
    case KEY_8:
    case KEY_DOWN:
        if (!isPaused) {
            setDirection(0, 1);
        }
        break;
    case KEY_4:
        if (!isPaused) {
            setDirection(-1, 0);
        }
        break;
    case KEY_0:
    case KEY_6:
        if (!isPaused) {
            setDirection(1, 0);
        }
        break;
    case KEY_MENU:
        if (isGameOver) {
            resetGame();
        } else {
            isPaused = !isPaused;
            showPause(isPaused);
        }
        kbd.counter = 0;
        SYSTEM_DelayMs(200);
        break;
    case KEY_EXIT:
        isPaused = false;
        isInitialized = false;
        break;
    default:
        break;
    }

    if (wasPaused == true && isPaused == false) {
        redrawPlayfield();
    }
}

static KEY_Code_t GetKey(void)
{
    KEY_Code_t btn = KEYBOARD_Poll();
    if (btn == KEY_INVALID && GPIO_IsPttPressed()) {
        btn = KEY_PTT;
    }
    return btn;
}

static void HandleUserInput(void)
{
    kbd.prev = kbd.current;
    kbd.current = GetKey();

    if (kbd.current != KEY_INVALID && kbd.current == kbd.prev) {
        kbd.counter = 1;
    } else {
        kbd.counter = 0;
    }

    if (kbd.counter == 1) {
        OnKeyDown(kbd.current);
    }
}

static void Tick(void)
{
    HandleUserInput();
    HandleUserInput();
}

static void RunGame(void)
{
#ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
    uint8_t frame = 0;
#endif

    kbd.current = KEY_INVALID;
    kbd.prev = KEY_INVALID;
    kbd.counter = 0;

    srand_custom((BK4819_ReadRegister(BK4819_REG_67) & 0x01FF) * gBatteryVoltageAverage * gEeprom.VfoInfo[0].pRX->Frequency);
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);

    resetGame();
    isInitialized = true;

    while (isInitialized) {
        Tick();

        if (!isPaused) {
            if (!stepSnake()) {
                isGameOver = true;
                isPaused = true;
                showGameOver();
            }

            if (gSnakeSettings.level_setting == 0) {
                uint8_t newLevel = getLevelFromScore(score);
                if (newLevel != snake_level) {
                    snake_level = newLevel;
                    food_count = 0;
                    clearFood();
                    buildObstacles();
                    placeFoodAll();
                    redrawPlayfield();
                }
            }

            moveFood();

            if (isBeep) {
                playBeep(tone);
                isBeep = false;
            }

#ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
            if ((frame++ & 0x03) == 0) {
                getScreenShot(false);
            }
#endif
        }

        drawScore();

        ST7565_BlitStatusLine();
        ST7565_BlitFullScreen();

        if (!isPaused) {
            uint8_t idx = (snake_level > 0) ? (uint8_t)(snake_level - 1) : 0;
            uint16_t base = gSnakeBaseDelay[idx];
            uint16_t min = gSnakeMinDelay[idx];
            uint16_t speedup = MIN((uint16_t)(score * gSnakeAccel[idx]), (base - min));
            uint16_t delay = base - speedup;
            if (delay < min) {
                delay = min;
            }
            SYSTEM_DelayMs(delay);
        } else {
            SYSTEM_DelayMs(30);
        }
    }
}

void APP_RunSnake(void)
{
    uint8_t selection = 0;

    kbd.current = KEY_INVALID;
    kbd.prev = KEY_INVALID;
    kbd.counter = 0;

    while (1) {
        KEY_Code_t key;

        DrawSnakeMenu(selection);

        while ((key = ReadKeyOnce()) == KEY_INVALID) {
            SYSTEM_DelayMs(30);
        }

        switch (key) {
        case KEY_UP:
        case KEY_2:
            selection = 0;
            break;
        case KEY_DOWN:
        case KEY_8:
            selection = 1;
            break;
        case KEY_MENU:
            if (selection == 0) {
                RunGame();
            } else {
                RunConfigMenu();
            }
            kbd.current = KEY_INVALID;
            kbd.prev = KEY_INVALID;
            kbd.counter = 0;
            break;
        case KEY_EXIT:
            return;
        default:
            break;
        }
    }
}
