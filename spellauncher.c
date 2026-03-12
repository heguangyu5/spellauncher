#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>
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

#define SCREEN_WIDTH    1400
#define SCREEN_HEIGHT   800

// 主题颜色
Color bg_color      = (Color){ 30, 30, 36, 255 };
Color box_color     = (Color){ 50, 50, 60, 255 };
Color text_color    = (Color){ 240, 240, 240, 255 };
Color meaning_color = (Color){ 180, 200, 255, 255 };
Color error_color   = (Color){ 200, 50, 50, 255 };
Color success_color = (Color){ 50, 200, 100, 255 };

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
int target_len;
char user_input[MAX_WORD_LEN] = {0};
int input_len = 0;

// 动画状态
float error_shake_timer  = 0.0f;
float success_anim_timer = 0.0f;
bool is_transitioning    = false;

// 资源
Font chinese_font;
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
void LoadWords() {
    FILE *f = fopen("words.txt", "r");
    if (!f) {
        perror("words.txt");
        exit(1);
    }

    char line[512];
    while (fgets(line, sizeof(line), f) && word_count < MAX_WORDS) {
        line[strcspn(line, "\r\n")] = 0;
        char *tab = strchr(line, '\t');
        if (tab) {
            *tab = '\0';
            strcpy(words[word_count].word, line);
            strcpy(words[word_count].meaning, tab + 1);
            word_count++;
        }
    }
    fclose(f);

    if (word_count == 0) {
        fprintf(stderr, "words.txt empty!\n");
        exit(2);
    }
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
    int codepointCount = 0;
    int *codepoints = GetRequiredCodepoints(&codepointCount);
    #ifdef DEV
    chinese_font = LoadFontEx("resources/SourceHanSansSC-Bold.ttf", 64, codepoints, codepointCount); 
    #else
    chinese_font = LoadFontFromMemory(".ttf", chinese_font_start, chinese_font_end - chinese_font_start, 64, codepoints, codepointCount); 
    #endif
    free(codepoints);
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
        int text_width = MeasureText(words[i].word, font_size);
        DrawText(words[i].word, SCREEN_WIDTH/2 - text_width/2, draw_y, font_size, done_color);
        draw_y += 40;
    }
}

void DrawCurrWord() {
    // 中部：中文释义
    // 开始高度: SCREEN_HEIGHT/2 - 100 = 400-100 = 300
    Vector2 meaning_size = MeasureTextEx(chinese_font, curr->meaning, 50, 2);
    DrawTextEx(chinese_font, curr->meaning,
               (Vector2){ SCREEN_WIDTH/2 - meaning_size.x/2, SCREEN_HEIGHT/2 - 100 },
               50, 2, meaning_color);

    // 中下部：输入框
    // 开始高度: SCREEN_HEIGHT / 2 = 400
    int box_width = 80;
    int box_height = 100;
    int spacing = 15;
    int total_width = (target_len * box_width) + ((target_len - 1) * spacing);
    int start_x = SCREEN_WIDTH / 2 - total_width / 2;
    int start_y = SCREEN_HEIGHT / 2;

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

    int start_y                = SCREEN_HEIGHT / 2;
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
    int bar_width = 600;
    int bar_height = 6;
    int bar_x = SCREEN_WIDTH/2 - bar_width/2;
    int bar_y = SCREEN_HEIGHT - 80;

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

// 启动 HMCL
void LaunchHMCL() {
    glob_t glob_result;
    if (glob("./HMCL-*.jar", 0, NULL, &glob_result) == 0) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "java -jar \"%s\" &", glob_result.gl_pathv[0]);
        system(cmd);
    }
}

int main(void) {
    // 设置无边框标志
    #ifndef DEV
    SetConfigFlags(FLAG_WINDOW_UNDECORATED);
    #endif

    // 初始化窗口与音频
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Spellauncher");
    InitAudioDevice();

    // 加载资源
    // 先加载单词本，收集所有中文
    LoadWords();
    // 按需加载字体纹理
    LoadFonts();
    // 加载音效
    LoadSounds();

    // 加载第一个单词
    NextWord();

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        if (current_word_index >= word_count) {
            LaunchHMCL();
            break; // 全部完成，退出循环
        }

        // --- 逻辑处理 ---
        HandleInput();
        UpdateTimer();

        // --- 绘制渲染 ---
        BeginDrawing();
        ClearBackground(bg_color);

        DrawSpelledWords();
        DrawCurrWord();
        DrawEncourageText();
        DrawProgress();

        EndDrawing();
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
