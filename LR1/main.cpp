#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <windows.h>
#include <algorithm>
#include <map>
#include <mutex>
#include <sstream>
#include <cctype>
#include <chrono>

using namespace std;

//мьютекс для синхронизации вывода
mutex output_mutex;

enum CountMode {
    VISIBLE_CHARS,    
    ALL_CHARS        
};

struct ThreadData {
    vector<string> lines;
    int thread_id = 0;
    int line_count = 0;
    int char_count = 0;
    map<string, int> word_count;
    CountMode count_mode = VISIBLE_CHARS;
    bool is_last_global_line = false;      
    bool file_ends_with_newline = false;
};

vector<string> splitIntoWords(const string& line) {
    vector<string> words;
    stringstream ss(line);
    string word;

    while (ss >> word) {
        string cleaned_word;
        for (char c : word) {
            if (isalnum(static_cast<unsigned char>(c))) {
                cleaned_word += tolower(static_cast<unsigned char>(c));
            }
        }
        if (!cleaned_word.empty()) {
            words.push_back(cleaned_word);
        }
    }
    return words;
}

DWORD WINAPI ProcessLines(LPVOID lpParam) {
    ThreadData* data = (ThreadData*)lpParam;
    data->line_count = 0;
    data->char_count = 0;

    for (size_t idx = 0; idx < data->lines.size(); ++idx) {
        const string& line = data->lines[idx];
        data->line_count++;

        if (data->count_mode == ALL_CHARS) {
            data->char_count += static_cast<int>(line.length());

            bool is_last_line_of_thread = (idx == data->lines.size() - 1);
            if (!(data->is_last_global_line && is_last_line_of_thread && !data->file_ends_with_newline)) {
                data->char_count++;
            }
        }
        else {
            data->char_count += static_cast<int>(line.length());
        }

        // Разделяем строку на слова
        vector<string> words = splitIntoWords(line);
        for (const string& word : words) {
            data->word_count[word]++;
        }
    }

    {
        //lock_guard<mutex> lock(output_mutex);
        cout << "Поток " << data->thread_id
            << " обработал " << data->lines.size() << " строк" << endl;
    }

    return 0;
}

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "RUSSIAN");
    cout << "=== Многопоточный анализатор файлов ===" << endl;

    if (argc < 3 || argc > 4) {
        cout << "Использование: myfiletool.exe <filename> <thread_count> [all|visible]" << endl;
        return 1;
    }

    string filename = argv[1];
    int thread_count = atoi(argv[2]);
    CountMode count_mode = VISIBLE_CHARS;

    if (argc == 4) {
        string mode = argv[3];
        if (mode == "all") {
            count_mode = ALL_CHARS;
            cout << "Режим: все символы (с учётом служебных)" << endl;
        }
        else if (mode == "visible") {
            count_mode = VISIBLE_CHARS;
            cout << "Режим: только видимые символы" << endl;
        }
        else {
            cout << "Ошибка: неизвестный режим \"" << mode << "\". Используйте all или visible." << endl;
            return 1;
        }
    }
    else {
        cout << "Режим: только видимые символы (по умолчанию)" << endl;
    }

    if (thread_count <= 0) {
        cout << "Ошибка: количество потоков должно быть положительным числом." << endl;
        return 1;
    }




    ifstream file(filename);
    if (!file.is_open()) {
        cout << "Ошибка: не могу открыть файл " << filename << endl;
        return 1;
    }

    vector<string> all_lines;
    string line;
    while (getline(file, line)) {
        all_lines.push_back(line);
    }
    file.close();

    int total_lines = static_cast<int>(all_lines.size());
    cout << "Прочитано строк: " << total_lines << endl;
    cout << "Количество потоков: " << thread_count << endl;

    if (total_lines == 0) {
        cout << "Файл пуст." << endl;
        return 0;
    }

    bool file_ends_with_newline = false;
    {
        ifstream check(filename, ios::binary);
        if (check.is_open()) {
            check.seekg(0, ios::end); //p->end
            if (check.tellg() > 0) {//p: length
                check.seekg(-1, ios::end);
                char last_char;
                check.get(last_char);
                file_ends_with_newline = (last_char == '\n');
            }
            check.close();
        }
    }

    if (thread_count > total_lines) {
        thread_count = total_lines;
        cout << "Внимание: потоков больше, чем строк. Использую " << thread_count << " поток(а)." << endl;
    }

    vector<ThreadData> thread_data(thread_count); //data of threads
    vector<HANDLE> threads(thread_count); //descriptors of threads

    int lines_per_thread = total_lines / thread_count;
    int remainder = total_lines % thread_count;
    int line_index = 0;

    auto start_time = chrono::high_resolution_clock::now();

    for (int i = 0; i < thread_count; i++) {
        int lines_for_this_thread = lines_per_thread + (i < remainder ? 1 : 0);

        for (int j = 0; j < lines_for_this_thread; j++) {
            thread_data[i].lines.push_back(all_lines[line_index++]);
        }

        thread_data[i].thread_id = i + 1;
        thread_data[i].count_mode = count_mode;

        if (i == thread_count - 1) {
            thread_data[i].is_last_global_line = true;
        }
        thread_data[i].file_ends_with_newline = file_ends_with_newline;

        threads[i] = CreateThread(NULL, 0, ProcessLines, &thread_data[i], 0, NULL);
        if (threads[i] == NULL) {
            cout << "Ошибка: не могу создать поток " << i + 1 << endl;
            return 1;
        }
        {
            lock_guard<mutex> lock(output_mutex);
            cout << "Создан поток " << (i + 1)
                << " для обработки " << lines_for_this_thread << " строк" << endl;
        }
    }

    WaitForMultipleObjects(thread_count, threads.data(), TRUE, INFINITE);

    int computed_lines = 0;
    int computed_chars = 0;
    map<string, int> global_word_count;

    for (int i = 0; i < thread_count; i++) {
        computed_lines += thread_data[i].line_count;
        computed_chars += thread_data[i].char_count;

        for (const auto& pair : thread_data[i].word_count) {
            global_word_count[pair.first] += pair.second;
        }
        CloseHandle(threads[i]);
    }

    auto end_time = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time);

    cout << "\n=== РЕЗУЛЬТАТЫ АНАЛИЗА ===" << endl;
    cout << "Общее количество строк: " << computed_lines << endl;
    cout << "Общее количество символов: " << computed_chars << endl;

    ifstream file_check(filename, ios::binary | ios::ate);
    if (file_check.is_open()) {
        long long file_size = file_check.tellg();
        file_check.close();
        cout << "Размер файла (байт): " << file_size << endl;
    }

    cout << "Время выполнения: " << duration.count() << " мс" << endl;

    if (!global_word_count.empty()) {
        auto most_common = max_element(
            global_word_count.begin(),
            global_word_count.end(),
            [](const pair<string, int>& a, const pair<string, int>& b) {
                return a.second < b.second;
            }
        );

        cout << "Самое частое слово: '" << most_common->first
            << "' (" << most_common->second << " раз)" << endl;

        cout << "\nТоп-10 слов:" << endl;
        vector<pair<string, int>> sorted_words(global_word_count.begin(), global_word_count.end());
        sort(sorted_words.begin(), sorted_words.end(),
            [](const auto& a, const auto& b) { return b.second < a.second; });

        int top_n = min(10, static_cast<int>(sorted_words.size()));
        for (int i = 0; i < top_n; i++) {
            cout << i + 1 << ") '" << sorted_words[i].first
                << "' : " << sorted_words[i].second << endl;
        }
    }

    cout << "Анализ завершен" << endl;
    return 0;
}
