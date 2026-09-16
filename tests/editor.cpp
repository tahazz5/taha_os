#include "../shared/text_editor.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
int main() {
    desktop::Editor e;
    assert(e.load("hello\nworld", 11));
    e.cursor = 5; assert(e.insert('!'));
    assert(e.size == 12 && e.data[5] == '!' && e.data[6] == '\n');
    e.backspace(); assert(e.size == 11 && e.data[5] == '\n');
    e.remove(); assert(e.size == 10 && e.data[5] == 'w');
    assert(e.load("abcd\nx\n12345", 12));
    e.cursor = 3; e.vertical(1, 10); assert(e.cursor == 6);
    e.vertical(1, 10); assert(e.cursor == 8);
    e.home(); assert(e.cursor == 7); e.end(); assert(e.cursor == 12);
    assert(e.load("abcdefghij", 10));
    e.cursor = 7; e.vertical(-1, 4); assert(e.cursor == 3);
    e.vertical(1, 4); assert(e.cursor == 7);
    unsigned row, col; e.position(8, 4, row, col); assert(row == 2 && col == 0);
    assert(e.at(1, 2, 4) == 6);
    const char invalid[] = {'a', '\0', 'b'};
    assert(!e.load(invalid, 3) && e.size == 10); // Failed load preserves the document.
    e.load("", 0);
    for (size_t i = 0; i < e.capacity; ++i) assert(e.insert('a'));
    assert(!e.insert('b') && e.size == e.capacity && e.data[e.size] == 0);
    e.cursor = 0; e.backspace(); assert(e.size == e.capacity);
    e.remove(); assert(e.size == e.capacity - 1 && e.data[e.size] == 0);
    assert(e.undo() && e.size == e.capacity && e.data[e.size] == 0);
    assert(e.redo() && e.size == e.capacity - 1);

    assert(e.load("abc", 3));
    assert(!e.undo() && !e.redo() && !e.modified);
    e.cursor = 1; assert(e.insert('X'));
    assert(e.undo() && !strcmp(e.data, "abc") && e.cursor == 1 && !e.modified);
    assert(e.redo() && !strcmp(e.data, "aXbc") && e.cursor == 2 && e.modified);
    e.mark_saved();
    e.backspace(); assert(!strcmp(e.data, "abc") && e.modified);
    assert(e.undo() && !strcmp(e.data, "aXbc") && e.cursor == 2 && !e.modified);
    assert(e.undo() && e.modified);
    assert(e.redo() && !e.modified);
    assert(e.redo() && e.modified);
    assert(e.undo() && !e.modified);
    e.remove(); assert(!strcmp(e.data, "aXc") && !e.redo());
    assert(e.undo() && !e.modified && e.cursor == 2);
    // Branching must not confuse a new edit with an abandoned saved revision.
    assert(e.undo()); assert(e.insert('Y')); e.mark_saved();
    assert(e.undo()); assert(e.insert('Z')); assert(e.modified && !e.redo());
    // Failed input and failed loads preserve both the document and its history.
    assert(!e.insert('\0') && !e.load(invalid, 3));
    assert(e.undo() && !strcmp(e.data, "abc"));
    assert(e.redo() && !strcmp(e.data, "aZbc"));
    assert(e.load("", 0));
    for (size_t i = 0; i < e.history_capacity + 10; ++i) assert(e.insert('a'));
    for (size_t i = 0; i < e.history_capacity; ++i) assert(e.undo());
    assert(!e.undo() && e.size == 10 && e.modified);
    for (size_t i = 0; i < e.history_capacity; ++i) assert(e.redo());
    assert(!e.redo() && e.size == e.history_capacity + 10);
    // Loading a different document prevents undo into the previous one.
    assert(e.load("new", 3) && !e.undo() && !e.redo() && !e.modified);
    puts("Editor tests passed (editing, navigation, bounds, undo/redo, save points, history eviction)");
}
