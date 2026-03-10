#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>
#include <strings.h>

typedef struct {
    char word[100];
    char meaning[256];
} WordItem;

typedef struct {
    GtkWidget **entries;
    int word_len;
    char current_word[100];
    int current_word_index;
    
    GtkWidget *window;
    GtkWidget *completed_label;
    GtkWidget *meaning_label;
    GtkWidget *hbox_entries;
    GtkWidget *progress_label; // 新增：进度标签
} AppContext;

WordItem *words = NULL;
int word_count = 0;

void load_words() {
    FILE *f = fopen("words.txt", "r");
    if (!f) {
        perror("无法打开 words.txt");
        exit(1);
    }
    char line[512];
    int capacity = 10;
    words = malloc(capacity * sizeof(WordItem));
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *tab = strchr(line, '\t');
        if (tab) {
            *tab = '\0';
            if (word_count >= capacity) {
                capacity *= 2;
                words = realloc(words, capacity * sizeof(WordItem));
            }
            strcpy(words[word_count].word, line);
            strcpy(words[word_count].meaning, tab + 1);
            word_count++;
        }
    }
    fclose(f);
}

void launch_hmcl() {
    glob_t glob_result;
    if (glob("./HMCL-*.jar", 0, NULL, &glob_result) == 0) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "java -jar \"%s\" &", glob_result.gl_pathv[0]);
        system(cmd);
    }
    gtk_main_quit();
}

gboolean clear_error_state(gpointer user_data) {
    AppContext *app = (AppContext *)user_data;
    for (int i = 0; i < app->word_len; i++) {
        GtkStyleContext *context = gtk_widget_get_style_context(app->entries[i]);
        gtk_style_context_remove_class(context, "error");
        gtk_entry_set_text(GTK_ENTRY(app->entries[i]), "");
    }
    gtk_widget_grab_focus(app->entries[0]);
    return G_SOURCE_REMOVE;
}

gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    AppContext *app = (AppContext *)user_data;
    int index = -1;
    for (int i = 0; i < app->word_len; i++) {
        if (app->entries[i] == widget) {
            index = i;
            break;
        }
    }

    if (event->keyval == GDK_KEY_BackSpace) {
        const char *text = gtk_entry_get_text(GTK_ENTRY(widget));
        if (strlen(text) == 0 && index > 0) {
            gtk_widget_grab_focus(app->entries[index - 1]);
            gtk_entry_set_text(GTK_ENTRY(app->entries[index - 1]), "");
            return TRUE;
        }
    }
    return FALSE;
}

void load_next_word(AppContext *app) {
    // 更新进度：显示 "已完成 / 总数"
    char progress_buf[64];
    snprintf(progress_buf, sizeof(progress_buf), 
             "<span size='20000' color='#999'>进度: %d / %d</span>", 
             app->current_word_index, word_count);
    gtk_label_set_markup(GTK_LABEL(app->progress_label), progress_buf);

    if (app->current_word_index >= word_count) {
        launch_hmcl();
        return;
    }

    WordItem *curr = &words[app->current_word_index];
    strcpy(app->current_word, curr->word);
    app->word_len = strlen(app->current_word);

    // 更新已完成单词列表 (一行一个)
    GString *completed_str = g_string_new("<span size='22000' color='#666'>");
    for(int i = 0; i < app->current_word_index; i++) {
        g_string_append_printf(completed_str, "%s\n", words[i].word);
    }
    g_string_append(completed_str, "</span>");
    gtk_label_set_markup(GTK_LABEL(app->completed_label), completed_str->str);
    g_string_free(completed_str, TRUE);

    // 更新释义
    char markup_meaning[512];
    snprintf(markup_meaning, sizeof(markup_meaning), "<span size='50000' weight='bold'>%s</span>", curr->meaning);
    gtk_label_set_markup(GTK_LABEL(app->meaning_label), markup_meaning);

    // 重置输入框
    GList *children = gtk_container_get_children(GTK_CONTAINER(app->hbox_entries));
    for (GList *iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    if (app->entries) free(app->entries);
    app->entries = malloc(app->word_len * sizeof(GtkWidget *));

    void on_entry_changed(GtkEditable *editable, gpointer user_data);
    for (int i = 0; i < app->word_len; i++) {
        app->entries[i] = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(app->entries[i]), 1);
        gtk_entry_set_alignment(GTK_ENTRY(app->entries[i]), 0.5);
        g_signal_connect(app->entries[i], "changed", G_CALLBACK(on_entry_changed), app);
        g_signal_connect(app->entries[i], "key-press-event", G_CALLBACK(on_key_press), app);
        gtk_box_pack_start(GTK_BOX(app->hbox_entries), app->entries[i], FALSE, FALSE, 0);
    }

    gtk_widget_show_all(app->hbox_entries);
    gtk_widget_grab_focus(app->entries[0]);
}

void on_entry_changed(GtkEditable *editable, gpointer user_data) {
    AppContext *app = (AppContext *)user_data;
    int filled_count = 0;
    char user_input[100] = {0};

    for (int i = 0; i < app->word_len; i++) {
        const char *txt = gtk_entry_get_text(GTK_ENTRY(app->entries[i]));
        if (strlen(txt) > 0) {
            user_input[i] = txt[0];
            filled_count++;
            if (GTK_WIDGET(editable) == app->entries[i] && i + 1 < app->word_len) {
                gtk_widget_grab_focus(app->entries[i + 1]);
            }
        }
    }

    if (filled_count == app->word_len) {
        if (strcasecmp(user_input, app->current_word) == 0) {
            app->current_word_index++;
            load_next_word(app);
        } else {
            for (int i = 0; i < app->word_len; i++) {
                gtk_style_context_add_class(gtk_widget_get_style_context(app->entries[i]), "error");
            }
            g_timeout_add(450, clear_error_state, app);
        }
    }
}

void apply_huge_style() {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css = 
        "entry {"
        "   font-size: 80px; font-weight: bold;"
        "   min-height: 140px; min-width: 100px;"
        "   margin: 5px; padding: 0px;"
        "   border-radius: 15px; border: 4px solid #ddd;"
        "}"
        "entry.error {"
        "   background-color: #ffebeb; color: #ff0000; border-color: #ff0000;"
        "}"
        "label { margin: 5px; }";
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    apply_huge_style();
    load_words();
    if (word_count == 0) return 1;

    AppContext app = {0};
    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "HMCL 单词解锁");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 1000, 900);
    gtk_window_set_position(GTK_WINDOW(app.window), GTK_WIN_POS_CENTER);
    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(main_vbox), 30);
    gtk_container_add(GTK_CONTAINER(app.window), main_vbox);

    // 1. 顶部：已完成列表 (带滚动条)
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled, -1, 200);
    app.completed_label = gtk_label_new("");
    gtk_container_add(GTK_CONTAINER(scrolled), app.completed_label);
    gtk_box_pack_start(GTK_BOX(main_vbox), scrolled, FALSE, FALSE, 10);

    // 2. 中部：释义
    app.meaning_label = gtk_label_new(NULL);
    gtk_box_pack_start(GTK_BOX(main_vbox), app.meaning_label, TRUE, TRUE, 10);

    // 3. 中下部：大输入框容器
    app.hbox_entries = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(app.hbox_entries, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(main_vbox), app.hbox_entries, FALSE, FALSE, 30);

    // 4. 底部右下角：进度显示
    app.progress_label = gtk_label_new("");
    gtk_widget_set_halign(app.progress_label, GTK_ALIGN_END); // 水平靠右
    gtk_widget_set_valign(app.progress_label, GTK_ALIGN_END); // 垂直靠下
    gtk_box_pack_end(GTK_BOX(main_vbox), app.progress_label, FALSE, FALSE, 0);

    load_next_word(&app);
    gtk_widget_show_all(app.window);
    gtk_main();

    if (app.entries) free(app.entries);
    free(words);
    return 0;
}
