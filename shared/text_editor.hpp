#pragma once
#include <stddef.h>

// A bounded ASCII document. Positions are byte offsets; the caret can sit at EOF.
namespace desktop {
struct Editor {
    static constexpr size_t capacity = 32768;
    char data[capacity + 1]{};
    size_t size = 0, cursor = 0;
    bool modified = false;
    static constexpr size_t history_capacity = 256;
private:
    struct Change {
        size_t position, before_cursor, after_cursor, before_revision, after_revision;
        char value;
        bool insertion;
    };
    Change history[history_capacity]{};
    size_t history_size = 0, history_position = 0;
    size_t revision = 0, saved_revision = 0, next_revision = 0;
    void record(size_t position, char value, bool insertion, size_t before_cursor) {
        history_size = history_position; // A new edit discards the redo branch.
        if (history_size == history_capacity) {
            for (size_t i = 1; i < history_size; ++i) history[i - 1] = history[i];
            --history_size;
        }
        history[history_size++] = {position, before_cursor, cursor, revision, ++next_revision, value, insertion};
        history_position = history_size; revision = next_revision;
        modified = revision != saved_revision;
    }
    void apply(const Change& change, bool forward) {
        if (change.insertion == forward) {
            for (size_t i = size; i > change.position; --i) data[i] = data[i - 1];
            data[change.position] = change.value; data[++size] = 0;
        } else {
            for (size_t i = change.position; i < size; ++i) data[i] = data[i + 1];
            --size;
        }
        cursor = forward ? change.after_cursor : change.before_cursor;
        revision = forward ? change.after_revision : change.before_revision;
        modified = revision != saved_revision;
    }
public:
    void mark_saved() { saved_revision = revision; modified = false; }
    bool undo() {
        if (!history_position) return false;
        apply(history[--history_position], false); return true;
    }
    bool redo() {
        if (history_position == history_size) return false;
        apply(history[history_position++], true); return true;
    }

    bool load(const char* source, size_t length) {
        if (length > capacity) return false;
        for (size_t i = 0; i < length; ++i)
            if (source[i] != '\n' && source[i] != '\t' && (source[i] < 32 || source[i] > 126)) return false;
        for (size_t i = 0; i < length; ++i) data[i] = source[i];
        data[length] = 0; size = length; cursor = 0; modified = false;
        history_size = history_position = revision = saved_revision = next_revision = 0;
        return true;
    }
    bool insert(char c) {
        if (size == capacity || (c != '\n' && c != '\t' && (c < 32 || c > 126))) return false;
        size_t position = cursor;
        for (size_t i = size; i > cursor; --i) data[i] = data[i - 1];
        data[cursor++] = c; data[++size] = 0; modified = true;
        record(position, c, true, position);
        return true;
    }
    void remove() {
        if (cursor == size) return;
        char value = data[cursor];
        for (size_t i = cursor; i < size; ++i) data[i] = data[i + 1];
        --size; modified = true;
        record(cursor, value, false, cursor);
    }
    void backspace() {
        if (cursor) {
            size_t before = cursor; --cursor; remove();
            history[history_position - 1].before_cursor = before;
        }
    }
    void position(size_t index, unsigned columns, unsigned& row, unsigned& col) const {
        row = col = 0;
        for (size_t i = 0; i < index && i < size; ++i) {
            if (data[i] == '\n' || ++col == columns) { ++row; col = 0; }
        }
    }
    size_t at(unsigned target_row, unsigned target_col, unsigned columns) const {
        unsigned row = 0, col = 0;
        for (size_t i = 0; i < size; ++i) {
            if (row == target_row && (col >= target_col || data[i] == '\n')) return i;
            if (data[i] == '\n' || ++col == columns) {
                if (row == target_row) return i; // Last cell of a wrapped row.
                ++row; col = 0;
            }
        }
        return size;
    }
    void vertical(int delta, unsigned columns) {
        unsigned row, col; position(cursor, columns, row, col);
        if (delta < 0 && row < unsigned(-delta)) cursor = at(0, col, columns);
        else cursor = at(row + delta, col, columns);
    }
    void home() { while (cursor && data[cursor - 1] != '\n') --cursor; }
    void end() { while (cursor < size && data[cursor] != '\n') ++cursor; }
};
}
