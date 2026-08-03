/**
 * @file struct_pointer_array_deep_dive.c
 * @brief [重要] 構造体・ポインタ・配列の相互関係を深掘りする学習用サンプル。
 * @details
 * - 目的: 組み込み開発で頻出する「構造体 x ポインタ x 配列」の組み合わせパターンを
 *   小さく確実な例で確認する。
 * - 対象読者: ポインタ・配列・構造体を単体では理解済みで、組み合わせた際の
 *   落とし穴やメモリレイアウトを復習したい人向け。
 * - 主な題材:
 *   1. 構造体ポインタの基本（-> 演算子 / malloc・free の対応）
 *   2. 構造体の配列（スタック配列とヒープ配列の違い）
 *   3. 配列とポインタの等価性・ポインタ演算
 *   4. int[3][4] / int*[2] / int(*)[4] の型の違い
 *   5. 構造体内の配列メンバ vs ポインタメンバ（サイズとコピー挙動の違い）
 *   6. 構造体に持たせた関数ポインタ（組み込みドライバの簡易vtableパターン）
 *   7. 自己参照構造体による単方向連結リスト
 *   8. ビット演算の基礎（AND/OR/XOR/NOT/シフト、SET/CLEAR/TOGGLE/CHECK）
 *   9. 構造体のビットフィールド（レジスタマッピングの表現、unionとの併用）
 *   10. 構造体のパディング/アライメントと良い構造体の作り方
 *
 * 実行手順:
 * cd learning_public   (リポジトリのルート)
 * gcc -std=c99 -Wall -Wextra -Wpedantic -g \
 *     c/structures/struct_pointer_array_deep_dive.c \
 *     -o c/structures/struct_pointer_array_deep_dive
 * ./c/structures/struct_pointer_array_deep_dive
 *
 * @note [推奨] Valgrind/AddressSanitizer でメモリリーク・不正アクセスがないことを確認する。
 * 例: gcc -std=c99 -Wall -Wextra -fsanitize=address,undefined -g \
 *     c/structures/struct_pointer_array_deep_dive.c -o /tmp/spa_asan && /tmp/spa_asan
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief セクションの見出しを表示します。
 * @param title const char* 見出し文字列
 * @return void
 */
static void print_title(const char *title) {
    printf("\n=== %s ===\n", title);
}

/* ------------------------------------------------------------------------
 * 1. 構造体ポインタの基本（-> 演算子 / malloc・free の対応）
 * ---------------------------------------------------------------------- */

/**
 * @brief センサー情報を表す構造体（id + 名前）。
 * @details 以降のセクションで「構造体ポインタ」「構造体の配列」の題材として使い回す。
 */
typedef struct {
    int id;
    char name[16];
} Sensor;

/**
 * @brief 構造体ポインタの基本（-> 演算子とmalloc/freeの対応）を確認します。
 * @details
 * - `p->member` は `(*p).member` の糖衣構文（syntax sugar）であることを、両方の書き方で
 *   同じ結果になることから確認する。
 * - スタック上の実体を指すポインタと、`malloc` でヒープに確保した実体を指すポインタを両方試す。
 * - [重要] `malloc` した数だけ必ず `free` する。free直後にポインタへNULLを代入しておくと、
 *   誤って解放済みメモリへアクセス（use-after-free）した場合に気付きやすくなる。
 * @return void
 *
 * 実行結果（例）:
 * (*p).id=1, p->id=1
 * p->name=temp
 * heap_sensor->id=2, name=humidity
 */
static void demonstrate_struct_pointer_basics(void) {
    print_title("1. struct pointer basics (-> / malloc-free)");

    /* スタック上に実体を確保し、そのアドレスをポインタで持つ */
    Sensor temp_sensor = {.id = 1, .name = "temp"};
    Sensor *p = &temp_sensor;

    /* (*p).id と p->id は同じ意味。組み込みコードでは -> が主流 */
    printf("(*p).id=%d, p->id=%d\n", (*p).id, p->id);
    printf("p->name=%s\n", p->name);

    /* ヒープ上に確保する場合は、確保した数だけ必ず free する */
    Sensor *heap_sensor = malloc(sizeof(Sensor));
    if (heap_sensor == NULL) {
        fprintf(stderr, "[error] malloc failed for heap_sensor\n");
        return;
    }
    heap_sensor->id = 2;
    strncpy(heap_sensor->name, "humidity", sizeof(heap_sensor->name) - 1);
    heap_sensor->name[sizeof(heap_sensor->name) - 1] = '\0';

    printf("heap_sensor->id=%d, name=%s\n", heap_sensor->id, heap_sensor->name);

    free(heap_sensor);
    heap_sensor = NULL; /* [推奨] free直後にNULL代入し、以降の誤アクセスを検出しやすくする */
}

/* ------------------------------------------------------------------------
 * 2. 構造体の配列（スタック配列 vs ヒープ配列）
 * ---------------------------------------------------------------------- */

/**
 * @brief 構造体の配列を、スタック確保とヒープ確保の両方のパターンで確認します。
 * @details
 * - スタック配列（`Sensor arr[3]`）: 要素数はコンパイル時に固定。関数を抜けると自動的に破棄される。
 * - ヒープ配列（`calloc` で確保）: 要素数を実行時に決められる代わりに、確保した側が
 *   「使い終わったら必ず `free` する」責任を負う。
 * - `calloc` は `malloc` と違い、確保した領域をゼロクリアしてから返す点も合わせて確認する。
 * @return void
 *
 * 実行結果（例）:
 * stack_sensors[0]: id=10 name=s0
 * stack_sensors[1]: id=11 name=s1
 * stack_sensors[2]: id=12 name=s2
 * heap_sensors[0]: id=100 name=h0
 * heap_sensors[1]: id=101 name=h1
 * heap_sensors[2]: id=102 name=h2
 * heap_sensors[3]: id=103 name=h3
 */
static void demonstrate_struct_array(void) {
    print_title("2. array of struct (stack vs heap)");

    /* スタック配列: サイズはコンパイル時固定、関数を抜けると消える */
    Sensor stack_sensors[3] = {
        {.id = 10, .name = "s0"},
        {.id = 11, .name = "s1"},
        {.id = 12, .name = "s2"},
    };
    for (size_t i = 0; i < sizeof(stack_sensors) / sizeof(stack_sensors[0]); i++) {
        printf("stack_sensors[%zu]: id=%d name=%s\n", i, stack_sensors[i].id, stack_sensors[i].name);
    }

    /* ヒープ配列: 実行時に個数を決められる。使い終わったら必ずfree */
    const size_t count = 4;
    Sensor *heap_sensors = calloc(count, sizeof(Sensor));
    if (heap_sensors == NULL) {
        fprintf(stderr, "[error] calloc failed for heap_sensors\n");
        return;
    }
    for (size_t i = 0; i < count; i++) {
        heap_sensors[i].id = (int)(100 + i);
        snprintf(heap_sensors[i].name, sizeof(heap_sensors[i].name), "h%zu", i);
    }
    for (size_t i = 0; i < count; i++) {
        printf("heap_sensors[%zu]: id=%d name=%s\n", i, heap_sensors[i].id, heap_sensors[i].name);
    }

    free(heap_sensors);
    heap_sensors = NULL;
}

/* ------------------------------------------------------------------------
 * 3. 配列とポインタの等価性・ポインタ演算
 * ---------------------------------------------------------------------- */

/**
 * @brief 配列とポインタの等価性、およびポインタ演算の基本を確認します。
 * @details
 * - 配列名 `values` は式中で使われると「先頭要素へのポインタ」に自動変換（減衰/decay）される。
 *   このため `values[i]`・`*(values + i)`・`p[i]`・`*(p + i)` はすべて同じ値を指す。
 * - ポインタ演算は「バイト数」ではなく「要素数」で進む（`p + 1` は `sizeof(*p)` バイト先を指す）。
 *   実際にアドレス差分をバイト単位で測って `sizeof(int)` と一致することを確認する。
 * - [悪い例] 配列の範囲外（末尾の1つ後ろより先）を指すポインタを参照すると未定義動作(UB)になる。
 *   末尾の1つ後ろまでを指すこと自体は合法（番兵として一般的に使われる）ため、その境界内に留める。
 * @return void
 *
 * 実行結果（例。アドレス値は実行のたびに変わる）:
 * i=0: values[i]=10 *(values+i)=10 p[i]=10 *(p+i)=10
 * i=1: values[i]=20 *(values+i)=20 p[i]=20 *(p+i)=20
 * i=2: values[i]=30 *(values+i)=30 p[i]=30 *(p+i)=30
 * i=3: values[i]=40 *(values+i)=40 p[i]=40 *(p+i)=40
 * i=4: values[i]=50 *(values+i)=50 p[i]=50 *(p+i)=50
 * &values[0]=0x..., &values[1]=0x..., diff(bytes)=4, sizeof(int)=4
 * last element via pointer: 50
 */
static void demonstrate_pointer_arithmetic(void) {
    print_title("3. array/pointer equivalence & pointer arithmetic");

    int values[5] = {10, 20, 30, 40, 50};
    int *p = values; /* 配列名は先頭要素へのポインタに「減衰(decay)」する */

    for (int i = 0; i < 5; i++) {
        /* values[i] と *(values+i) と p[i] と *(p+i) はすべて同じ値 */
        printf("i=%d: values[i]=%d *(values+i)=%d p[i]=%d *(p+i)=%d\n",
               i, values[i], *(values + i), p[i], *(p + i));
    }

    /* [重要] ポインタ演算は「要素単位」で進む。sizeof(int)バイトを手動で足す必要はない */
    printf("&values[0]=%p, &values[1]=%p, diff(bytes)=%ld, sizeof(int)=%zu\n",
           (void *)&values[0], (void *)&values[1],
           (long)((char *)&values[1] - (char *)&values[0]), sizeof(int));

    /* 配列の「一つ後ろ」までのポインタは合法（番兵として一般的）。
     * [悪い例] それ以上進めて参照すると未定義動作(UB)になるため、ここでは境界内に留める */
    int *end = values + 5;
    printf("last element via pointer: %d\n", *(end - 1));
}

/* ------------------------------------------------------------------------
 * 4. 多次元配列 / ポインタ配列 / 配列へのポインタ
 * ---------------------------------------------------------------------- */

/**
 * @brief 「真の2次元配列」「ポインタの配列」「配列へのポインタ」という3つの異なる型を比較します。
 * @details
 * - (A) `int matrix[3][4]`: 12個のintが1つの連続したメモリブロックとして並ぶ、正真正銘の2次元配列。
 * - (B) `int *ptr_array[2]`: 「ポインタが2個並んだ配列」。各ポインタは別々の場所にある配列を指してよく、
 *   各行のメモリが連続している保証はない（ジャグ配列や `char *argv[]` と同じ形）。
 * - (C) `int (*row_ptr)[4]`: 「int[4]という1行分」を単位として指すポインタ。`int**` とは別の型であり、
 *   `matrix` を代入できるのはこの型だけ（`int**` には代入できない）。
 * - sizeofの結果とポインタ演算の進み幅（1つ進めると何バイト動くか）を実測して、
 *   3つのメモリレイアウトの違いを体感する。
 * @return void
 *
 * 実行結果（例。アドレス値は実行のたびに変わる／sizeof(int*)は64bit環境で8を想定）:
 * (A) matrix[1][2]=7
 * (B) ptr_array[1][2]=202 (row1[2])
 * (C) row_ptr[1][2]=7 (matrix[1][2]と同じ)
 * sizeof(matrix)=48 (=3*4*sizeof(int)=48)
 * sizeof(ptr_array)=16 (=2*sizeof(int*)=16, 実データは別アドレス)
 * row_ptr=0x..., row_ptr+1=0x..., diff(bytes)=16 (expected 16)
 */
static void demonstrate_array_of_pointers_vs_pointer_to_array(void) {
    print_title("4. int[3][4] vs int*[2] vs int(*)[4]");

    /* (A) 真の2次元配列: メモリは連続した1ブロック(3*4*sizeof(int)) */
    int matrix[3][4] = {
        {1, 2, 3, 4},
        {5, 6, 7, 8},
        {9, 10, 11, 12},
    };

    /* (B) ポインタの配列: 各要素が「別々に確保された」配列を指す（ジャグ配列や文字列配列 char*[] と同じ形） */
    int row0[3] = {100, 101, 102};
    int row1[3] = {200, 201, 202};
    int *ptr_array[2] = {row0, row1};

    /* (C) 配列へのポインタ: matrix の「行」を1つの単位として指す。
     * 型は int(*)[4] であり int** ではない点に注意 */
    int (*row_ptr)[4] = matrix;

    printf("(A) matrix[1][2]=%d\n", matrix[1][2]);
    printf("(B) ptr_array[1][2]=%d (row1[2])\n", ptr_array[1][2]);
    printf("(C) row_ptr[1][2]=%d (matrix[1][2]と同じ)\n", row_ptr[1][2]);

    /* [重要] sizeofの違い: 真の2次元配列は全体サイズ、ポインタ配列は「ポインタ分」のみ */
    printf("sizeof(matrix)=%zu (=3*4*sizeof(int)=%zu)\n", sizeof(matrix), 3 * 4 * sizeof(int));
    printf("sizeof(ptr_array)=%zu (=2*sizeof(int*)=%zu, 実データは別アドレス)\n",
           sizeof(ptr_array), 2 * sizeof(int *));

    /* row_ptrを1つ進めると「1行分(4要素)」アドレスが進む */
    printf("row_ptr=%p, row_ptr+1=%p, diff(bytes)=%ld (expected %zu)\n",
           (void *)row_ptr, (void *)(row_ptr + 1),
           (long)((char *)(row_ptr + 1) - (char *)row_ptr), 4 * sizeof(int));
}

/* ------------------------------------------------------------------------
 * 5. 構造体内の配列メンバ vs ポインタメンバ（サイズ・コピー挙動の違い）
 * ---------------------------------------------------------------------- */

/**
 * @brief 固定長バッファを「配列メンバ」として構造体の内部に直接持たせた型。
 */
typedef struct {
    char buf[16]; /* 配列メンバ: 構造体の内部にデータそのものを持つ */
} EmbeddedBuf;

/**
 * @brief バッファ本体を持たず、「ポインタメンバ」として外部データのアドレスだけを持つ型。
 */
typedef struct {
    char *buf; /* ポインタメンバ: 構造体はアドレスだけを持つ（実体は外部） */
} PointerBuf;

/**
 * @brief 構造体メンバが「配列」か「ポインタ」かによって、構造体代入時のコピー挙動が
 * 変わることを確認します。
 * @details
 * - `EmbeddedBuf`（配列メンバ）を代入すると、配列の中身までまるごとコピーされる。
 *   コピー後の2つの変数は独立しており、一方を変更してももう一方には影響しない（深いコピー相当）。
 * - `PointerBuf`（ポインタメンバ）を代入すると、コピーされるのは「アドレスの値」だけ。
 *   2つの変数は同じメモリを指したままなので、一方を変更するともう一方にも影響する（浅いコピー）。
 * - `sizeof` の違い（配列メンバはバッファ本体を含むぶん大きい／ポインタメンバは常にポインタ1個分）も
 *   合わせて確認する。
 * @return void
 *
 * 実行結果（例。sizeof(char*)は64bit環境で8を想定）:
 * a1.buf=hello, a2.buf=Hello (独立している)
 * b1.buf=World, b2.buf=World (同じメモリを指しているため両方変わる)
 * sizeof(EmbeddedBuf)=16 (バッファ本体を含む)
 * sizeof(PointerBuf)=8 (ポインタ1個分=8、バッファ本体は含まない)
 */
static void demonstrate_struct_member_array_vs_pointer(void) {
    print_title("5. struct member: array vs pointer");

    EmbeddedBuf a1 = {.buf = "hello"};
    EmbeddedBuf a2 = a1; /* 構造体代入 = 配列の中身までまるごとコピーされる（深いコピー相当） */
    a2.buf[0] = 'H';
    printf("a1.buf=%s, a2.buf=%s (独立している)\n", a1.buf, a2.buf);

    char shared[16] = "world";
    PointerBuf b1 = {.buf = shared};
    PointerBuf b2 = b1; /* 構造体代入だが、コピーされるのは「アドレスの値」だけ（浅いコピー） */
    b2.buf[0] = 'W';
    printf("b1.buf=%s, b2.buf=%s (同じメモリを指しているため両方変わる)\n", b1.buf, b2.buf);

    printf("sizeof(EmbeddedBuf)=%zu (バッファ本体を含む)\n", sizeof(EmbeddedBuf));
    printf("sizeof(PointerBuf)=%zu (ポインタ1個分=%zu、バッファ本体は含まない)\n",
           sizeof(PointerBuf), sizeof(char *));
}

/* ------------------------------------------------------------------------
 * 6. 構造体に持たせた関数ポインタ（組み込みドライバの簡易vtableパターン）
 * ---------------------------------------------------------------------- */

/**
 * @brief 「読み取り処理」を関数ポインタとして持つ、センサードライバを表す構造体。
 * @details `read` メンバを差し替えるだけで、呼び出し側のコードを変えずに実装を切り替えられる。
 * これはC言語でポリモーフィズム（多態性）を実現する代表的な手法で、C++の仮想関数テーブル
 * （vtable）が内部的にやっていることに相当する。
 */
typedef struct SensorDriver {
    const char *name;
    int (*read)(const struct SensorDriver *self); /* 関数ポインタメンバ */
} SensorDriver;

/**
 * @brief 固定値25を返す `read` の実装例（実機ではセンサレジスタの読み取り処理に相当）。
 * @param self const SensorDriver* 呼び出し元のドライバ自身へのポインタ（このダミー実装では未使用）
 * @return int 読み取り値（常に25）
 */
static int read_fixed_25(const SensorDriver *self) {
    (void)self;
    return 25;
}

/**
 * @brief 固定値60を返す `read` の実装例（実機ではセンサレジスタの読み取り処理に相当）。
 * @param self const SensorDriver* 呼び出し元のドライバ自身へのポインタ（このダミー実装では未使用）
 * @return int 読み取り値（常に60）
 */
static int read_fixed_60(const SensorDriver *self) {
    (void)self;
    return 60;
}

/**
 * @brief 構造体に関数ポインタを持たせ、簡易的なvtable/ドライバ切替パターンを確認します。
 * @details
 * - `temp_driver` と `humidity_driver` は `read` の実装が異なるだけで、呼び出し方
 *   （`drivers[i]->read(drivers[i])`）はどちらも同じ。
 * - 実機で新しいセンサを追加する場合も、`read` に対応する関数を実装して差し替えるだけでよく、
 *   呼び出し側のループ処理には手を入れずに済む。
 * @return void
 *
 * 実行結果（例）:
 * driver[0] name=temperature value=25
 * driver[1] name=humidity value=60
 */
static void demonstrate_function_pointer_in_struct(void) {
    print_title("6. function pointer member (simple vtable / driver pattern)");

    /* 実機では read を「実際にセンサレジスタを読むコード」に差し替える。
     * 呼び出し側は SensorDriver* だけを見れば、中身の実装差を意識せず呼べる */
    SensorDriver temp_driver = {.name = "temperature", .read = read_fixed_25};
    SensorDriver humidity_driver = {.name = "humidity", .read = read_fixed_60};

    const SensorDriver *drivers[2] = {&temp_driver, &humidity_driver};
    for (size_t i = 0; i < 2; i++) {
        printf("driver[%zu] name=%s value=%d\n", i, drivers[i]->name, drivers[i]->read(drivers[i]));
    }
}

/* ------------------------------------------------------------------------
 * 7. 自己参照構造体による単方向連結リスト
 * ---------------------------------------------------------------------- */

/**
 * @brief 単方向連結リストの1ノード。自分自身の型へのポインタを持つ「自己参照構造体」。
 * @details
 * 構造体は「自分自身を値として」直接メンバに持つことはできない（無限にサイズが必要になってしまうため）。
 * しかし「自分自身の型へのポインタ」であればサイズが常に固定（環境ごとのポインタ幅）なので持てる。
 * これを利用して、実行時に任意個のノードをmallocで繋いでいくのが連結リストの基本形。
 */
typedef struct Node {
    int value;
    struct Node *next;
} Node;

/**
 * @brief 連結リストの先頭にノードを追加します。
 * @param head Node* 現在の先頭ノード（空リストの場合はNULL）
 * @param value int 追加する値
 * @return Node* 新しい先頭ノード。malloc に失敗した場合はエラーを出力し、元の head をそのまま返す。
 */
static Node *list_push_front(Node *head, int value) {
    Node *node = malloc(sizeof(Node));
    if (node == NULL) {
        fprintf(stderr, "[error] malloc failed in list_push_front\n");
        return head;
    }
    node->value = value;
    node->next = head;
    return node;
}

/**
 * @brief 連結リストの内容を先頭から順に表示します。
 * @param head const Node* 先頭ノード（空リストの場合はNULL）
 * @return void
 */
static void list_print(const Node *head) {
    printf("list:");
    for (const Node *cur = head; cur != NULL; cur = cur->next) {
        printf(" %d ->", cur->value);
    }
    printf(" NULL\n");
}

/**
 * @brief 連結リストの全ノードを解放します。
 * @details [重要] `free(cur)` する前に `cur->next` を `next` へ退避しておく。
 * 解放してから `cur->next` を読むと、解放済みメモリを参照する use-after-free になるため。
 * @param head Node* 先頭ノード（NULL可。その場合は何もしない）
 * @return void
 */
static void list_free(Node *head) {
    Node *cur = head;
    while (cur != NULL) {
        Node *next = cur->next;
        free(cur);
        cur = next;
    }
}

/**
 * @brief 自己参照構造体を使い、単方向連結リストの構築・表示・解放を一通り確認します。
 * @details `list_push_front` で3ノードを追加し、`list_print` で表示、最後に `list_free` で
 * 全ノードを解放してメモリリークがないことを（ASan/Valgrindで）確認できる状態にする。
 * @return void
 *
 * 実行結果（例。1,2,3の順でpush_frontしたので、リストは先頭挿入の逆順になる）:
 * list: 1 -> 2 -> 3 -> NULL
 */
static void demonstrate_self_referential_struct(void) {
    print_title("7. self-referential struct (singly linked list)");

    Node *head = NULL;
    head = list_push_front(head, 3);
    head = list_push_front(head, 2);
    head = list_push_front(head, 1);

    list_print(head);

    list_free(head);
    head = NULL;
}

/* ------------------------------------------------------------------------
 * 8. ビット演算の基礎（AND/OR/XOR/NOT/シフト、SET/CLEAR/TOGGLE/CHECK）
 * ---------------------------------------------------------------------- */

/* [推奨] ビット位置をマジックナンバーではなく名前付き定数にする */
#define BIT_POWER    (1u << 0) /* bit0: 電源ON/OFF */
#define BIT_ERROR    (1u << 1) /* bit1: エラー状態 */
#define BIT_READY    (1u << 2) /* bit2: 準備完了 */
#define BIT_OVERHEAT (1u << 3) /* bit3: 過熱警告 */

/**
 * @brief 8bit値を「0x.. (2進数8桁)」の形式で表示します。
 * @param label const char* 表示ラベル
 * @param value uint8_t 表示対象の値
 * @return void
 */
static void print_bits_u8(const char *label, uint8_t value) {
    printf("%s = 0x%02X (", label, value);
    for (int i = 7; i >= 0; i--) {
        putchar((value & (1u << i)) ? '1' : '0');
    }
    printf(")\n");
}

/**
 * @brief ビット演算（AND/OR/XOR/NOT/シフト）の代表的な使い方を確認します。
 * @details
 * - SET（ビットを立てる）: `flags |= BIT_X`（対象ビットだけ1にし、他ビットはそのまま）
 * - CLEAR（ビットを下ろす）: `flags &= ~BIT_X`（対象ビットだけ0にし、他ビットはそのまま）
 * - TOGGLE（反転する）: `flags ^= BIT_X`（対象ビットだけ 0→1 または 1→0 に反転）
 * - CHECK（立っているか調べる）: `flags & BIT_X` が非0なら立っている
 * - シフト演算 `1u << n` で「2のn乗」のビットパターンを作れることを、n=0..3で確認する。
 * - [重要] 符号あり整数のシフトは、オーバーフローや右シフトの実装依存（算術/論理どちらになるか）が
 *   絡み未定義動作(UB)の温床になりやすい。ビット操作には基本的に unsigned 型（ここでは uint8_t）を使う。
 * @return void
 *
 * 実行結果（例）:
 * initial = 0x00 (00000000)
 * after SET POWER,READY = 0x05 (00000101)
 * is POWER on? yes
 * is ERROR on? no
 * after CLEAR READY = 0x01 (00000001)
 * after TOGGLE ERROR (0->1) = 0x03 (00000011)
 * after TOGGLE ERROR (1->0) = 0x01 (00000001)
 * 1u << 0 = 1
 * 1u << 1 = 2
 * 1u << 2 = 4
 * 1u << 3 = 8
 */
static void demonstrate_bit_operations(void) {
    print_title("8. bit operations (AND/OR/XOR/NOT/shift)");

    uint8_t flags = 0;
    print_bits_u8("initial", flags);

    /* SET: OR で対象ビットだけを1にする（他ビットは変えない） */
    flags |= BIT_POWER;
    flags |= BIT_READY;
    print_bits_u8("after SET POWER,READY", flags);

    /* CHECK: AND でマスクし、非0なら立っている */
    printf("is POWER on? %s\n", (flags & BIT_POWER) ? "yes" : "no");
    printf("is ERROR on? %s\n", (flags & BIT_ERROR) ? "yes" : "no");

    /* CLEAR: NOTしたマスクとAND して対象ビットだけを0にする */
    flags &= (uint8_t)~BIT_READY;
    print_bits_u8("after CLEAR READY", flags);

    /* TOGGLE: XOR で対象ビットだけ反転する（0→1、1→0） */
    flags ^= BIT_ERROR;
    print_bits_u8("after TOGGLE ERROR (0->1)", flags);
    flags ^= BIT_ERROR;
    print_bits_u8("after TOGGLE ERROR (1->0)", flags);

    /* シフト: 左シフトは「倍々」、右シフトは「半分ずつ」（符号なし整数の場合） */
    for (int i = 0; i < 4; i++) {
        printf("1u << %d = %u\n", i, 1u << i);
    }

    /* [重要] 符号あり整数の左シフトによるオーバーフロー、右シフトの実装依存(算術/論理)はUBの温床。
     * 組み込みでビット操作をするときは基本的に unsigned 型を使う。 */
}

/* ------------------------------------------------------------------------
 * 9. 構造体のビットフィールド（レジスタマッピングの表現、unionとの併用）
 * ---------------------------------------------------------------------- */

/**
 * @brief ステータスレジスタを表すビットフィールド構造体。
 * @details
 * `unsigned int name : N` という書き方（ビットフィールド）で、1つの整数の中に複数の
 * フラグをNビットずつ詰め込める。組み込みでハードウェアのレジスタ仕様書（bit0=電源, bit1=エラー…）
 * をそのままコードへ写し取るときによく使われる。
 * [重要] ビットフィールドのビット順・パディングは処理系依存(implementation-defined)。
 * 同一コンパイラ/同一ターゲットでは安定するが、他コンパイラへの移植性はない。
 * 実務ではベンダー提供ヘッダ（同一コンパイラ前提）で使われることが多い。
 */
typedef struct {
    unsigned int power    : 1; /* bit0 */
    unsigned int error    : 1; /* bit1 */
    unsigned int ready    : 1; /* bit2 */
    unsigned int overheat : 1; /* bit3 */
    unsigned int reserved : 4; /* bit4-7: 未使用ビット（サイズを揃えるため明示的に埋める） */
} StatusRegisterBits;

/**
 * @brief ビット単位アクセス(bits)とバイト単位アクセス(raw)を同じメモリに重ね合わせるunion。
 * @details
 * union は「同じメモリ領域を、複数のメンバの型として読み書きできる」機能。ここでは
 * `bits.power` のようなフィールド単位のアクセスと、`raw` によるバイト単位の一括読み書き
 * （実機でレジスタを1回のI/O命令でまとめて読み書きするイメージ）を両立させる、
 * 組み込みレジスタ定義でよく見るパターンを再現している。
 * [注意] 厳密なエイリアシング規則(strict aliasing)の観点ではグレーだが、
 * 多くの組み込み向けコンパイラ(GCC/Clang含む)は拡張として実用上サポートしている。
 * 移植先コンパイラでの挙動は必ず確認すること。
 */
typedef union {
    StatusRegisterBits bits;
    uint8_t raw;
} StatusRegister;

/**
 * @brief 構造体のビットフィールドを使って、レジスタのようなビット単位のデータを操作します。
 * @details
 * - `reg.bits.power = 1` のようにフィールド名で個別ビットを操作できることを確認する。
 * - `reg.raw` に直接ビット演算（`|=` 等）を行うと、`reg.bits` 側からも同じ変化として見える
 *   ことを確認する（同じメモリを異なる見え方でアクセスしているだけのため）。
 * - このセクション（ビットフィールド）とセクション8（手動マスク+シフト）は、
 *   最終的に同じビットパターンを作れる。フィールド名で読みやすく書けるか、
 *   バイト列との対応を厳密に制御したいか、でどちらを使うか判断する。
 * @return void
 *
 * 実行結果（例。sizeof(StatusRegisterBits)は「合計8bitしか使っていないのに4byte」になりやすい点に注意。
 * ビットフィールドの土台（allocation unit）は基本型 `unsigned int` 単位で確保されるため）:
 * sizeof(StatusRegisterBits)=4, sizeof(StatusRegister)=4
 * after set power,ready via bit-field: raw=0x05
 * after raw |= overheat: bits.overheat=1, raw=0x0D
 * after clear power via bit-field: raw=0x0C
 */
static void demonstrate_struct_bitfield(void) {
    print_title("9. struct bit-field (register mapping)");

    printf("sizeof(StatusRegisterBits)=%zu, sizeof(StatusRegister)=%zu\n",
           sizeof(StatusRegisterBits), sizeof(StatusRegister));

    StatusRegister reg = {.raw = 0};
    reg.bits.power = 1;
    reg.bits.ready = 1;
    printf("after set power,ready via bit-field: raw=0x%02X\n", reg.raw);

    /* raw を直接いじる（実機でレジスタを一括読み書きするイメージ）と、
     * bits 側からも同じビットとして見える */
    reg.raw |= (uint8_t)BIT_OVERHEAT;
    printf("after raw |= overheat: bits.overheat=%u, raw=0x%02X\n", reg.bits.overheat, reg.raw);

    reg.bits.power = 0;
    printf("after clear power via bit-field: raw=0x%02X\n", reg.raw);

    /* ビットフィールド方式(このセクション)と手動マスク方式(セクション8)は等価な結果になる。
     * - ビットフィールド: フィールド名でアクセスでき読みやすいが、ビット順がコンパイラ依存。
     * - 手動マスク+シフト: 冗長だが、通信プロトコルやファイルフォーマットのバイト列と
     *   1対1で対応させたい用途（移植性が必要な用途）ではこちらが安全。 */
}

/* ------------------------------------------------------------------------
 * 10. 構造体のパディング/アライメントと良い構造体の作り方
 * ---------------------------------------------------------------------- */

/**
 * @brief [悪い例] メンバの並び順を意識していない構造体。
 * @details アライメントの都合でメンバ間に「隙間(パディング)」が挿入されやすく、
 * 各メンバのサイズを単純合計した値より `sizeof` が大きくなりやすい。
 */
typedef struct {
    char flag;    /* 1 byte */
    int count;    /* 4 byte境界に合わせる必要があり、flagの後にパディングが入りやすい */
    char kind;    /* 1 byte */
    double value; /* 8 byte境界に合わせる必要があり、kindの後に大きくパディングが入りやすい */
} PoorlyOrderedStatus;

/**
 * @brief [良い例] アライメント要求が大きいメンバから小さいメンバの順に並べた構造体。
 * @details メンバは `PoorlyOrderedStatus` と同じ4つだが、並び順を変えるだけでパディングが
 * 減り、`sizeof` が小さくなりやすい（実測は `demonstrate_struct_padding` で確認する）。
 */
typedef struct {
    double value; /* 8 byte。最初に置けば後続の型は自然にアライメントが揃いやすい */
    int count;    /* 4 byte */
    char flag;    /* 1 byte */
    char kind;    /* 1 byte（構造体末尾側にまとまるので詰め物が少なくて済みやすい） */
} WellOrderedStatus;

/**
 * @brief 構造体メンバのバイトオフセットを表示します。
 * @param struct_name const char* 構造体名（表示用ラベル）
 * @param member_name const char* メンバ名（表示用ラベル）
 * @param offset size_t `offsetof` マクロで得たオフセット値（構造体先頭からのバイト数）
 * @return void
 */
static void print_offset(const char *struct_name, const char *member_name, size_t offset) {
    printf("  offsetof(%s, %s) = %zu\n", struct_name, member_name, offset);
}

/**
 * @brief 構造体のパディング/アライメントの仕組みと、良い構造体設計の指針を確認します。
 * @details
 * - メンバ構成は同じだが並び順だけが異なる `PoorlyOrderedStatus` と `WellOrderedStatus` を用意し、
 *   `sizeof`（構造体全体のサイズ）と `offsetof`（各メンバの先頭からのバイト位置）を実測して、
 *   並び順がメモリレイアウトに与える影響を数値で確認する。
 * - なぜパディングが必要なのか（アライメント要求）と、それを踏まえた構造体設計の指針を
 *   関数末尾のコメントにまとめる。
 * @return void
 */
static void demonstrate_struct_padding(void) {
    print_title("10. struct padding / alignment & how to design a good struct");

    printf("sizeof(PoorlyOrderedStatus)=%zu\n", sizeof(PoorlyOrderedStatus));
    print_offset("PoorlyOrderedStatus", "flag", offsetof(PoorlyOrderedStatus, flag));
    print_offset("PoorlyOrderedStatus", "count", offsetof(PoorlyOrderedStatus, count));
    print_offset("PoorlyOrderedStatus", "kind", offsetof(PoorlyOrderedStatus, kind));
    print_offset("PoorlyOrderedStatus", "value", offsetof(PoorlyOrderedStatus, value));

    printf("sizeof(WellOrderedStatus)=%zu\n", sizeof(WellOrderedStatus));
    print_offset("WellOrderedStatus", "value", offsetof(WellOrderedStatus, value));
    print_offset("WellOrderedStatus", "count", offsetof(WellOrderedStatus, count));
    print_offset("WellOrderedStatus", "flag", offsetof(WellOrderedStatus, flag));
    print_offset("WellOrderedStatus", "kind", offsetof(WellOrderedStatus, kind));

    /* [図解] バイト単位のメモリレイアウト（x86_64 / gcc・clang での実測値に基づく一例。
     * 処理系・ターゲットアーキテクチャによって数値は変わり得るので、あくまで一例として見ること）。
     * 凡例: F=flag(1B) C=count(4B) K=kind(1B) V=value(8B) .=パディング(未使用領域)
     *
     * [悪い例] PoorlyOrderedStatus: char, int, char, double の宣言順のまま → 隙間が2箇所、計10byte
     *
     *   byte: 0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   16   17   18   19   20   21   22   23
     *         F    .    .    .    C    C    C    C    K    .    .    .    .    .    .    .    V    V    V    V    V    V    V    V
     *
     *   +----------+--------+-------+---------------------------------------------+
     *   | offset   | size   | field | 備考                                        |
     *   +----------+--------+-------+---------------------------------------------+
     *   | 0        | 1 byte | flag  |                                             |
     *   | 1        | 3 byte | (pad) | count を4byte境界(offset=4)に合わせるため    |
     *   | 4        | 4 byte | count |                                             |
     *   | 8        | 1 byte | kind  |                                             |
     *   | 9        | 7 byte | (pad) | value を8byte境界(offset=16)に合わせるため   |
     *   | 16       | 8 byte | value |                                             |
     *   +----------+--------+-------+---------------------------------------------+
     *   合計 24 byte（データ本体は 1+4+1+8=14 byte、パディングだけで 10 byte を占める）
     *
     * [良い例] WellOrderedStatus: double, int, char, char の順に並べ替え → 隙間は末尾に1箇所だけ、計2byte
     *
     *   byte: 0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15
     *         V    V    V    V    V    V    V    V    C    C    C    C    F    K    .    .
     *
     *   +----------+--------+-------+---------------------------------------------+
     *   | offset   | size   | field | 備考                                        |
     *   +----------+--------+-------+---------------------------------------------+
     *   | 0        | 8 byte | value |                                             |
     *   | 8        | 4 byte | count |                                             |
     *   | 12       | 1 byte | flag  |                                             |
     *   | 13       | 1 byte | kind  |                                             |
     *   | 14       | 2 byte | (pad) | 構造体全体を最大アライメント(8byte)の倍数に  |
     *   |          |        |       | 切り上げるため（配列化したとき2要素目以降も  |
     *   |          |        |       | 正しくアライメントさせるため）              |
     *   +----------+--------+-------+---------------------------------------------+
     *   合計 16 byte（データ本体は同じ 14 byte、パディングは 2 byte のみ）
     *
     * → 同じ4メンバ・同じ合計データ量(14byte)でも、並び順だけで 24byte と 16byte（差33%）に変わる。
     *
     * [重要] なぜパディングが入るか:
     * - 各メンバは「自分の型のアライメント要求（多くの処理系ではそのサイズと同じ）」の
     *   倍数のアドレスに配置される（例: int→4byte境界、double→8byte境界が典型的だが、
     *   具体的な値は処理系・ターゲットアーキテクチャ依存）
     * - 構造体全体のサイズも「メンバの最大アライメント要求」の倍数に切り上げられる
     *   （構造体を配列にしたとき、2番目以降の要素も正しくアライメントされるようにするため）
     *
     * [良い構造体の作り方（指針）]
     * 1. アライメント要求が大きい型（double/ポインタ等）から小さい型（char）の順に並べる
     *    → 途中のパディングを減らし、余りを末尾に寄せられる
     * 2. sizeof/offsetof は「決め打ちせず必ずコードで確認する」
     *    （処理系・ターゲット・コンパイラオプションで変わり得るため）
     * 3. どうしてもバイト単位で詰めたい（通信プロトコル/ファイルフォーマット等）場合は
     *    `#pragma pack` や `__attribute__((packed))` のようなコンパイラ拡張を検討するが、
     *    - 一部アーキテクチャでは未整列アクセスが遅い、または例外/クラッシュを起こす
     *    - コンパイラ依存になり移植性が下がる
     *    ため、「本当に必要な箇所だけ」に限定して使う
     * 4. 大量に配列化する構造体（センサーデータのリングバッファ等）は、
     *    パディング削減がメモリ使用量に直結するため、特に並び順を意識する価値がある
     */
}

/**
 * @brief エントリポイント。ファイル冒頭 @details の1〜10を順番に実行します。
 * @return int 終了コード（常に0）
 */
int main(void) {
    demonstrate_struct_pointer_basics();
    demonstrate_struct_array();
    demonstrate_pointer_arithmetic();
    demonstrate_array_of_pointers_vs_pointer_to_array();
    demonstrate_struct_member_array_vs_pointer();
    demonstrate_function_pointer_in_struct();
    demonstrate_self_referential_struct();
    demonstrate_bit_operations();
    demonstrate_struct_bitfield();
    demonstrate_struct_padding();
    return 0;
}
