#ifdef _WIN32
    // 提前定义这些宏，告诉 Windows "我不需要你那些庞大且老旧的图形界面组件"
    // 这能从源头上掐断 90% 的命名冲突，还能加快编译速度
    #define WIN32_LEAN_AND_MEAN
    #define NOGDI       // 屏蔽 GDI (解决 Rectangle 冲突)
    #define NOUSER      // 屏蔽 USER (解决 CloseWindow, ShowCursor 冲突)
    
    // 包含所需的 Windows 头文件
    #include <windows.h>
    #include <shellapi.h>
    
    // 手动补全被 NOUSER 屏蔽掉的必要常量
    #ifndef SW_SHOWNORMAL
        #define SW_SHOWNORMAL 1
    #endif

    // 2. 核心防御：暴力卸载可能的残留宏/符号
    // 即使上面定义了 NOGDI 和 NOUSER，某些 Windows 版本依然可能漏网
    // 用 #undef 强制解除占用，把这些名字"还给" raylib
    #undef Rectangle
    #undef CloseWindow
    #undef ShowCursor
    #undef PlaySound
#endif

#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#define GUI_WINDOW_FILE_DIALOG_IMPLEMENTATION
#include "gui_window_file_dialog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef DEV
extern const unsigned char chinese_font_start[];
extern const unsigned char chinese_font_end[];
extern const unsigned char type_wav_start[];
extern const unsigned char type_wav_end[];
extern const unsigned char error_wav_start[];
extern const unsigned char error_wav_end[];
extern const unsigned char success_wav_start[];
extern const unsigned char success_wav_end[];
#endif

#define SCREEN_WIDTH    1600
#define SCREEN_HEIGHT   900

// 主题颜色
Color bg_color      = (Color){ 30, 30, 36, 255 };
Color box_color     = (Color){ 50, 50, 60, 255 };
Color text_color    = (Color){ 240, 240, 240, 255 };
Color meaning_color = (Color){ 180, 200, 255, 255 };
Color error_color   = (Color){ 200, 50, 50, 255 };
Color success_color = (Color){ 50, 200, 100, 255 };

// 场景
enum GameScene {
    CHOOSE_WORDS,
    SPELL,
    LAUNCH
};
enum GameScene curScene = CHOOSE_WORDS;

// 文件选择框
GuiWindowFileDialogState fileDialogState = { 0 };

// 单词本
#define MAX_WORDS 1000
#define MAX_WORD_LEN 100
#define MAX_MEANING_LEN 256

typedef struct {
    char word[MAX_WORD_LEN];
    char meaning[MAX_MEANING_LEN];
} WordItem;

WordItem words[MAX_WORDS];
int word_count = 0;

// 状态与输入
int current_word_index = -1;
WordItem *curr;
bool curr_has_image = false;
Texture2D curr_texture;
int target_len;
char user_input[MAX_WORD_LEN] = {0};
int input_len = 0;

// 动画状态
float error_shake_timer  = 0.0f;
float success_anim_timer = 0.0f;
bool is_transitioning    = false;

// 资源
Font chinese_font;
bool chinese_font_loaded = false;
Sound sound_type;
Sound sound_error;
Sound sound_success;

// 鼓励文字
const char *encourage_texts[] = {
    "Nice!",
    "Excellent!",
    "Awesome!",
    "太棒了！",
    "优秀！",
    "完美！"
};
#define ENCOURAGE_TEXTS_COUNT 6

// 加载单词库
int LoadWords(const char *filename) {
    word_count = 0;
    current_word_index = -1;

    FILE *f = fopen(filename, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f) && word_count < MAX_WORDS) {
            line[strcspn(line, "\r\n")] = 0;
            char *tab = strchr(line, '\t');
            if (tab) {
                *tab = '\0';
                if (strlen(line) < MAX_WORD_LEN && strlen(tab + 1) < MAX_MEANING_LEN) {
                    strcpy(words[word_count].word, line);
                    strcpy(words[word_count].meaning, tab + 1);
                    word_count++;
                }
            }
        }
        fclose(f);
    }

    return word_count;
}

// 提取需要的字符（ASCII + 单词库中的中文 + 界面UI可能用到的中文）
int* GetRequiredCodepoints(int *count) {
    int max_codepoints = 5000; // 预估最大不同字符数，5000足够应付大部分单词本
    int *codepoints = (int *)malloc(max_codepoints * sizeof(int));
    int current_count = 0;

    // 1. 载入基础 ASCII 字符 (32~126)
    for (int i = 32; i <= 126; i++) {
        codepoints[current_count++] = i;
    }

    // 你可以在这里加上 UI 里可能用到的额外中文字符（如果后续你想把 Excellent 换成中文）
    const char *ui_text = "太棒了优秀完美！!"; 

    // 内部帮助逻辑：解析文本并去重存入数组
    void AddTextCodepoints(const char *text) {
        int length = strlen(text);
        int bytesProcessed = 0;
        
        while (bytesProcessed < length) {
            int codepointSize = 0;
            // GetCodepoint 是 Raylib 内置的 UTF-8 解码函数
            int codepoint = GetCodepoint(&text[bytesProcessed], &codepointSize);
            
            // 简单的去重检查
            bool exists = false;
            for (int j = 0; j < current_count; j++) {
                if (codepoints[j] == codepoint) {
                    exists = true;
                    break;
                }
            }

            // 如果不存在且没超出容量，加入数组
            if (!exists && current_count < max_codepoints) {
                codepoints[current_count++] = codepoint;
            }

            bytesProcessed += codepointSize;
        }
    }

    // 2. 提取 UI 额外文本
    AddTextCodepoints(ui_text);

    // 3. 遍历单词本，提取所有释义中的中文字符
    for (int i = 0; i < word_count; i++) {
        AddTextCodepoints(words[i].meaning);
    }

    *count = current_count;
    return codepoints;
}

void LoadFonts() {
    if (chinese_font_loaded) {
        UnloadFont(chinese_font);
    }

    int codepointCount = 0;
    int *codepoints = GetRequiredCodepoints(&codepointCount);
    #ifdef DEV
    chinese_font = LoadFontEx("resources/SourceHanSansSC-Bold.ttf", 64, codepoints, codepointCount); 
    #else
    chinese_font = LoadFontFromMemory(".ttf", chinese_font_start, chinese_font_end - chinese_font_start, 64, codepoints, codepointCount); 
    #endif
    free(codepoints);
    chinese_font_loaded = true;
    // 推荐开启双线性滤波：这样哪怕字号缩放，字体边缘依然平滑不会有马赛克
    SetTextureFilter(chinese_font.texture, TEXTURE_FILTER_BILINEAR);
}

void LoadSounds() {
    #ifdef DEV
    sound_type    = LoadSound("resources/type.wav");
    sound_error   = LoadSound("resources/error.wav");
    sound_success = LoadSound("resources/success.wav");
    #else
    sound_type    = LoadSoundFromWave(LoadWaveFromMemory(".wav", type_wav_start, type_wav_end - type_wav_start));
    sound_error   = LoadSoundFromWave(LoadWaveFromMemory(".wav", error_wav_start, error_wav_end - error_wav_start));
    sound_success = LoadSoundFromWave(LoadWaveFromMemory(".wav", success_wav_start, success_wav_end - success_wav_start));
    #endif
}

void NextWord() {
    is_transitioning = false;

    current_word_index++;
    curr = &words[current_word_index];
    target_len = strlen(curr->word);

    input_len = 0;
    memset(user_input, 0, sizeof(user_input));
    
    if (current_word_index >= word_count) {
        curScene = LAUNCH;
    } else {
        if (curr_has_image) {
            UnloadTexture(curr_texture);
        }
        char tmp[MAX_WORD_LEN];
        char *tp = tmp;
        char *p = curr->word;
        while (*p) {
            if (*p == ' ') {
                *tp = '_';
            } else {
                *tp = *p;
            }
            p++;
            tp++;
        }
        *tp = 0;
        const char *image = TextFormat("%s" PATH_SEPERATOR "images" PATH_SEPERATOR "%s.png", fileDialogState.dirPathText, tmp);
        curr_has_image = FileExists(image);
        if (curr_has_image) {
            curr_texture = LoadTexture(image);
        }
    }
}

void HandleInput() {
    if (is_transitioning) {
        return;
    }

    // 获取键盘输入
    int key = GetCharPressed();
    while (key > 0) {
        if ((key >= 32) && (key <= 126) && (input_len < target_len)) {
            user_input[input_len] = (char)key;
            input_len++;
            PlaySound(sound_type);
        }
        key = GetCharPressed();
    }

    // 退格键
    if (IsKeyPressed(KEY_BACKSPACE) && input_len > 0) {
        input_len--;
        user_input[input_len] = '\0';
        PlaySound(sound_type);
    }

    // 检查单词是否填满
    if (input_len == target_len) {
        if (strcasecmp(user_input, curr->word) == 0) {
            // 正确
            is_transitioning = true;
            success_anim_timer = 1.0f;
            PlaySound(sound_success);
        } else {
            // 错误
            error_shake_timer = 0.5f;
            input_len = 0;
            memset(user_input, 0, sizeof(user_input));
            PlaySound(sound_error);
        }
    }
}

void UpdateTimer() {
    // 更新动画计时器
    if (error_shake_timer > 0) {
        error_shake_timer -= GetFrameTime();
    }
    if (success_anim_timer > 0) {
        success_anim_timer -= GetFrameTime();
        if (success_anim_timer <= 0) {
            // 动画结束，进入下一个单词
            NextWord();
        }
    }
}

void DrawSpelledWords() {
    // 顶部：已完成单词（带渐变消失效果）
    // 显示最近的5个单词,顶部高度: 50 + 40 * 5 = 250
    int draw_y = 50;
    for (int i = (current_word_index - 5 < 0) ? 0 : current_word_index - 5; i < current_word_index; i++) {
        float alpha_factor = 1.0f - ((current_word_index - 1 - i) * 0.2f);
        Color done_color = (Color){ 100, 200, 100, (unsigned char)(255 * alpha_factor) };
        int font_size = 25;
        Vector2 meaning_size = MeasureTextEx(chinese_font, words[i].meaning, font_size, 2);
        int text_width = MeasureText(words[i].word, font_size);
        int draw_x = SCREEN_WIDTH/2 - (meaning_size.x + text_width)/2 - 10;
        DrawTextEx(chinese_font, words[i].meaning, (Vector2){ draw_x, draw_y }, font_size, 2, done_color);
        DrawText(words[i].word, draw_x + meaning_size.x + 10, draw_y, font_size, done_color);
        draw_y += 40;
    }
}

void DrawCurrWordImage() {
    if (curr_has_image) {
        // y: 550
        float scale = 200.0f / curr_texture.height;
        DrawTextureEx(
            curr_texture, 
            (Vector2){ SCREEN_WIDTH/2 - curr_texture.width * scale / 2, 550 },
            0.0f,
            scale,
            WHITE
        );
    }
}

void DrawCurrWord() {
    // 中部：中文释义
    // y: 300 ~ 350
    Vector2 meaning_size = MeasureTextEx(chinese_font, curr->meaning, 50, 2);
    DrawTextEx(chinese_font, curr->meaning,
               (Vector2){ SCREEN_WIDTH/2 - meaning_size.x/2, 300 },
               50, 2, meaning_color);

    // 中下部：输入框
    // y: 400 ~ 500
    int box_width = 80;
    int box_height = 100;
    int spacing = 15;
    int total_width = (target_len * box_width) + ((target_len - 1) * spacing);
    int start_x = SCREEN_WIDTH / 2 - total_width / 2;
    int start_y = 400;

    // 震动偏移量
    float offset_x = 0;
    if (error_shake_timer > 0) {
        offset_x = sin(error_shake_timer * 40.0f) * 10.0f; // 快速正弦波实现震动
    }

    for (int i = 0; i < target_len; i++) {
        Rectangle rect = { start_x + i * (box_width + spacing) + offset_x, start_y, box_width, box_height };

        Color current_box_color = box_color;
        if (error_shake_timer > 0) current_box_color = error_color; // 错误闪红
        if (success_anim_timer > 0) current_box_color = success_color; // 正确闪绿

        // 绘制带有圆角的背景框
        DrawRectangleRounded(rect, 0.2f, 10, current_box_color);

        // 绘制输入的字母
        if (i < input_len || (is_transitioning && i < target_len)) {
            char letter[2] = { is_transitioning ? curr->word[i] : user_input[i], '\0' };
            int letter_width = MeasureText(letter, 60);
            DrawText(letter, rect.x + box_width/2 - letter_width/2, rect.y + 20, 60, text_color);
        }
    }
}

void DrawEncourageText() {
    // 鼓励特效（正确时向上飘起的文字）
    if (success_anim_timer <= 0) {
        return;
    }

    int start_y                = 400;
    float anim_progress        = 1.0f - success_anim_timer; // 0.0 到 1.0
    int float_y                = start_y - 50 - (int)(anim_progress * 100);
    Color encourage_color      = (Color){ 255, 215, 0, (unsigned char)(255 * (1.0f - anim_progress)) }; // 金色并淡出
    int encourage_text_index   = current_word_index % ENCOURAGE_TEXTS_COUNT;
    const char *encourage_text = encourage_texts[encourage_text_index];
    int font_size              = 50;
    int spacing                = 2;
    if (encourage_text_index < 3) {
        int text_width = MeasureText(encourage_text, font_size);
        DrawText(encourage_text, SCREEN_WIDTH/2 - text_width/2, float_y, font_size, encourage_color);
    } else {
        Vector2 text_size = MeasureTextEx(chinese_font, encourage_text, font_size, spacing);
        DrawTextEx(chinese_font, encourage_text,
                   (Vector2){ SCREEN_WIDTH/2 - text_size.x/2, float_y }, font_size, spacing, encourage_color);
    }
}

void DrawProgress() {
    // 底部：进度显示
    int bar_width = 1200;
    int bar_height = 6;
    int bar_x = SCREEN_WIDTH/2 - bar_width/2;
    int bar_y = SCREEN_HEIGHT - 60;

    // 进度条背景
    DrawRectangleRounded((Rectangle){bar_x, bar_y, bar_width, bar_height}, 1.0f, 10, (Color){ 220, 230, 240, 255 });

    // 进度条填充 (增加平滑过渡计算)
    float progress = (float)current_word_index / word_count;
    DrawRectangleRounded((Rectangle){bar_x, bar_y, bar_width * progress, bar_height}, 1.0f, 10, (Color){ 46, 204, 113, 255 });

    // 进度文字
    char progress_text[64];
    snprintf(progress_text, sizeof(progress_text), "%d / %d", current_word_index, word_count);
    DrawText(progress_text, bar_x + bar_width + 20, bar_y - 2, 10, GRAY);
}

// 选择文件
const char *ChooseFile(Rectangle pos, const char *ext) {
    if (fileDialogState.SelectFilePressed) {
        fileDialogState.SelectFilePressed = false;
        if (IsFileExtension(fileDialogState.fileNameText, ext)) {
            return TextFormat("%s" PATH_SEPERATOR "%s", fileDialogState.dirPathText, fileDialogState.fileNameText);
        }
    }

    if (fileDialogState.windowActive) {
        GuiLock();
    }

    if (GuiButton(pos, GuiIconText(ICON_FILE_OPEN, "Open"))) {
        fileDialogState.windowActive = true;
    }

    GuiUnlock();

    GuiWindowFileDialog(&fileDialogState);

    return NULL;
}

// 烟花秀
typedef struct {
    Vector2 position;
    Vector2 velocity;
    Color color;
    float life;
    float maxLife;
    float size;
    bool active;
    bool flicker;
} Particle;

typedef struct {
    Vector2 position;
    Vector2 velocity;
    Color color;
    bool exploded;
    bool active;
    int type; 
    float flashRadius;
    Particle *particles; // 动态分配粒子数组
    int particleCount;
} Firework;

void InitFirework(Firework *f, int pCount, int screenWidth, int screenHeight, int typeRange) {
    f->active = true;
    f->exploded = false;
    f->flashRadius = 0;
    f->particleCount = pCount;
    f->particles = (Particle *)calloc(pCount, sizeof(Particle));
    f->type = GetRandomValue(0, typeRange);
    f->position = (Vector2){ (float)GetRandomValue(200, screenWidth - 200), (float)screenHeight };
    f->velocity = (Vector2){ (float)GetRandomValue(-100, 100), (float)GetRandomValue(-1100, -750) };
    f->color = ColorFromHSV((float)GetRandomValue(0, 360), 0.8f, 1.0f);
}

void CleanFirework(Firework *f) {
    if (f->particles) free(f->particles);
}

// --- 版本 1: 经典款 (Classic) - 简洁明快 ---
void ShowFireworkClassic(int sw, int sh) {
    const int maxF = 15;
    const int pPerF = 100;
    Firework f[15] = {0};
    float start = GetTime();

    while (GetTime() - start < 10.0 && !WindowShouldClose()) {
        if (GetRandomValue(1, 100) <= 5) {
            for(int i=0; i<maxF; i++) if(!f[i].active) { InitFirework(&f[i], pPerF, sw, sh, 0); break; }
        }
        BeginDrawing();
        ClearBackground(BLACK);
        for(int i=0; i<maxF; i++) {
            if(!f[i].active) continue;
            float dt = GetFrameTime();
            if(!f[i].exploded) {
                f[i].position.y += f[i].velocity.y * dt; f[i].velocity.y += 400.0f * dt;
                DrawCircleV(f[i].position, 4.0f, f[i].color);
                if(f[i].velocity.y >= 0) {
                    f[i].exploded = true;
                    for(int p=0; p<pPerF; p++) {
                        f[i].particles[p].active = true; f[i].particles[p].position = f[i].position;
                        float ang = GetRandomValue(0, 360)*DEG2RAD, spd = GetRandomValue(50, 300);
                        f[i].particles[p].velocity = (Vector2){cosf(ang)*spd, sinf(ang)*spd};
                        f[i].particles[p].life = 1.0f; f[i].particles[p].color = f[i].color;
                    }
                }
            } else {
                bool alive = false;
                for(int p=0; p<pPerF; p++) {
                    if(!f[i].particles[p].active) continue;
                    alive = true;
                    f[i].particles[p].position.x += f[i].particles[p].velocity.x * dt;
                    f[i].particles[p].position.y += f[i].particles[p].velocity.y * dt;
                    f[i].particles[p].life -= dt;
                    DrawCircleV(f[i].particles[p].position, 2.0f, Fade(f[i].particles[p].color, f[i].particles[p].life));
                    if(f[i].particles[p].life <= 0) f[i].particles[p].active = false;
                }
                if(!alive) { CleanFirework(&f[i]); f[i].active = false; }
            }
        }
        DrawText("GREAT JOB!", sw/2 - 100, sh/2, 40, RAYWHITE);
        EndDrawing();
    }
}

// --- 版本 2: 灵动款 (Vibrant) - 带有流光残影和加法混合 ---
void ShowFireworkVibrant(int sw, int sh) {
    const int maxF = 25;
    const int pPerF = 200;
    Firework f[25] = {0};
    float start = GetTime();

    while (GetTime() - start < 10.0 && !WindowShouldClose()) {
        if (GetRandomValue(1, 100) <= 7) {
            for(int i=0; i<maxF; i++) if(!f[i].active) { InitFirework(&f[i], pPerF, sw, sh, 2); break; }
        }
        BeginDrawing();
        DrawRectangle(0, 0, sw, sh, (Color){0, 0, 0, 40});
        BeginBlendMode(BLEND_ADDITIVE);
        for(int i=0; i<maxF; i++) {
            if(!f[i].active) continue;
            float dt = GetFrameTime();
            if(!f[i].exploded) {
                f[i].position.x += f[i].velocity.x*dt; f[i].position.y += f[i].velocity.y*dt; f[i].velocity.y += 450.0f*dt;
                DrawCircleV(f[i].position, 5.0f, f[i].color);
                if(f[i].velocity.y >= -20) {
                    f[i].exploded = true;
                    for(int p=0; p<pPerF; p++) {
                        f[i].particles[p].active = true; f[i].particles[p].position = f[i].position;
                        float ang = GetRandomValue(0, 360)*DEG2RAD, spd = GetRandomValue(100, 450);
                        f[i].particles[p].velocity = (Vector2){cosf(ang)*spd, sinf(ang)*spd};
                        f[i].particles[p].maxLife = (float)GetRandomValue(10, 20)/10.0f; f[i].particles[p].life = f[i].particles[p].maxLife;
                        f[i].particles[p].color = f[i].color;
                    }
                }
            } else {
                bool alive = false;
                for(int p=0; p<pPerF; p++) {
                    if(!f[i].particles[p].active) continue;
                    alive = true;
                    f[i].particles[p].velocity.y += 100.0f*dt;
                    f[i].particles[p].position.x += f[i].particles[p].velocity.x*dt;
                    f[i].particles[p].position.y += f[i].particles[p].velocity.y*dt;
                    f[i].particles[p].life -= dt;
                    DrawCircleV(f[i].particles[p].position, 3.0f * (f[i].particles[p].life/f[i].particles[p].maxLife), Fade(f[i].particles[p].color, f[i].particles[p].life/f[i].particles[p].maxLife));
                    if(f[i].particles[p].life <= 0) f[i].particles[p].active = false;
                }
                if(!alive) { CleanFirework(&f[i]); f[i].active = false; }
            }
        }
        EndBlendMode();
        DrawText("WELL DONE!", sw/2 - 120, sh/3, 50, GOLD);
        EndDrawing();
    }
}

// --- 版本 3: 豪华款 (Grand) - 针对 1600x900 优化的海量粒子与闪烁 ---
void ShowFireworkGrand(int sw, int sh) {
    const int maxF = 40;
    const int pPerF = 400;
    Firework f[40] = {0};
    float start = GetTime();

    while (GetTime() - start < 10.0 && !WindowShouldClose()) {
        float elapsed = GetTime() - start;
        int chance = (elapsed > 7.0f) ? 18 : 8;
        if (GetRandomValue(1, 100) <= chance) {
            for(int i=0; i<maxF; i++) if(!f[i].active) { InitFirework(&f[i], pPerF, sw, sh, 3); break; }
        }
        BeginDrawing();
        DrawRectangle(0, 0, sw, sh, (Color){5, 5, 15, 30});
        BeginBlendMode(BLEND_ADDITIVE);
        for(int i=0; i<maxF; i++) {
            if(!f[i].active) continue;
            float dt = GetFrameTime();
            if(!f[i].exploded) {
                f[i].position.y += f[i].velocity.y*dt; f[i].velocity.y += 500.0f*dt;
                DrawCircleV(f[i].position, 6.0f, WHITE);
                if(f[i].velocity.y >= -10) {
                    f[i].exploded = true; f[i].flashRadius = 200.0f;
                    for(int p=0; p<pPerF; p++) {
                        f[i].particles[p].active = true; f[i].particles[p].position = f[i].position;
                        float ang = GetRandomValue(0, 360)*DEG2RAD, spd = GetRandomValue(50, 600);
                        f[i].particles[p].velocity = (Vector2){cosf(ang)*spd, sinf(ang)*spd};
                        f[i].particles[p].maxLife = (float)GetRandomValue(15, 35)/10.0f; f[i].particles[p].life = f[i].particles[p].maxLife;
                        f[i].particles[p].color = f[i].color; f[i].particles[p].flicker = (GetRandomValue(1,10)>7);
                    }
                }
            } else {
                bool alive = false;
                if(f[i].flashRadius > 0) { DrawCircleV(f[i].position, f[i].flashRadius, Fade(f[i].color, 0.2f)); f[i].flashRadius -= 600.0f*dt; }
                for(int p=0; p<pPerF; p++) {
                    if(!f[i].particles[p].active) continue;
                    alive = true;
                    f[i].particles[p].velocity.x *= 0.97f; f[i].particles[p].velocity.y *= 0.97f; f[i].particles[p].velocity.y += 90.0f*dt;
                    f[i].particles[p].position.x += f[i].particles[p].velocity.x*dt; f[i].particles[p].position.y += f[i].particles[p].velocity.y*dt;
                    f[i].particles[p].life -= dt;
                    Color pCol = Fade(f[i].particles[p].color, f[i].particles[p].life/f[i].particles[p].maxLife);
                    if(f[i].particles[p].flicker && GetRandomValue(1,10)>5) pCol = WHITE;
                    DrawCircleV(f[i].particles[p].position, 4.0f * (f[i].particles[p].life/f[i].particles[p].maxLife), pCol);
                    if(f[i].particles[p].life <= 0) f[i].particles[p].active = false;
                }
                if(!alive) { CleanFirework(&f[i]); f[i].active = false; }
            }
        }
        EndBlendMode();
        DrawText("CONGRATULATIONS!", sw/2 - 250, sh/2 - 30, 60, GOLD);
        EndDrawing();
    }
}

void PlayRandomVictoryShow(int sw, int sh) {
    int choice = GetRandomValue(0, 2);

    switch(choice) {
        case 0: ShowFireworkClassic(sw, sh); break;
        case 1: ShowFireworkVibrant(sw, sh); break;
        case 2: ShowFireworkGrand(sw, sh); break;
    }
}

int main(int argc, char *argv[]) {
    // 设置无边框标志
    #ifndef DEV
    SetConfigFlags(FLAG_WINDOW_UNDECORATED);
    #endif

    // 初始化窗口与音频
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Spellauncher");
    InitAudioDevice();

    // 加载音效
    LoadSounds();

    // 初始化文件选择框状态
    fileDialogState = InitGuiWindowFileDialog(GetWorkingDirectory());

    // 尝试加载 words.txt
    bool can_back = true;
    if (LoadWords("words.txt") > 0) {
        // 设置了默认单词表,不能返回自行选择
        can_back = false;
        // 按需加载字体纹理
        LoadFonts();
        // 加载第一个单词
        NextWord();
        // 开始SPELL
        curScene = SPELL;
    }

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        if (curScene == SPELL) {
            // --- 逻辑处理 ---
            HandleInput();
            UpdateTimer();

            // --- 绘制渲染 ---
            BeginDrawing();
            ClearBackground(bg_color);

            DrawSpelledWords();
            DrawCurrWordImage();
            DrawCurrWord();
            DrawEncourageText();
            DrawProgress();

            if (can_back) {
                // 绘制返回按钮
                if (GuiLabelButton((Rectangle){SCREEN_WIDTH - 70, 10, 60, 30}, GuiIconText(ICON_UNDO_FILL, "Back"))) {
                    curScene = CHOOSE_WORDS;
                }
            }

            EndDrawing();
        } else if (curScene == CHOOSE_WORDS) {
            // 找到 words.txt
            BeginDrawing();
            ClearBackground(bg_color);

            int font_size = 25;
            int text_width = MeasureText("Choose Your Word List", font_size);
            DrawText("Choose Your Word List", SCREEN_WIDTH/2 - text_width/2, SCREEN_HEIGHT / 2 - 50, font_size, text_color);
            const char *filename = ChooseFile((Rectangle){ SCREEN_WIDTH/2 - 70, SCREEN_HEIGHT/2, 140, 30 }, ".txt");
            if (filename && LoadWords(filename) > 0) {
                // 按需加载字体纹理
                LoadFonts();
                // 加载第一个单词
                NextWord();
                // 开始SPELL
                curScene = SPELL;
            }

            EndDrawing();
        } else {
            PlayRandomVictoryShow(SCREEN_WIDTH, SCREEN_HEIGHT);
            if (argc > 1) {
                #ifdef _WIN32
                ShellExecuteA(NULL, "open", argv[1], NULL, NULL, SW_SHOWNORMAL);
                #else
                system(argv[1]);
                #endif
            }
            break;
        }
    }

    // 清理资源
    UnloadFont(chinese_font);
    UnloadSound(sound_type);
    UnloadSound(sound_error);
    UnloadSound(sound_success);
    CloseAudioDevice();
    CloseWindow();

    return 0;
}
