/**
 * C言語 Hello World プログラム
 * 概要: C言語の基本的な構文と関数の使用方法のサンプル
 * 主な仕様:
 * - printf を使用したコンソール出力
 * - 関数の定義と呼び出し
 * - 構造体を使用したデータ管理
 * - 基本的なエラーハンドリング
 * 制限事項: C99以降が推奨
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 人物情報を格納する構造体
 */
typedef struct {
    char name[100];  /**< 名前 */
    int age;         /**< 年齢 */
} Person;

/**
 * @brief テスト用の関数1 - 基本的な計算
 * @param a 第一引数
 * @param b 第二引数
 * @return a + b の結果
 */
int testFunction1(int a, int b) {
    printf("testFunction1が呼び出されました: 引数 a=%d, b=%d\n", a, b);
    return a + b;
}

/**
 * @brief テスト用の関数2 - 文字列処理
 * @param str 処理する文字列
 */
void testFunction2(const char* str) {
    if (str == NULL) {
        printf("エラー: testFunction2に NULL ポインタが渡されました\n");
        return;
    }
    
    printf("testFunction2が呼び出されました: 文字列 \"%s\"\n", str);
    printf("文字列の長さ: %zu文字\n", strlen(str));
}

/**
 * @brief テスト用の関数3 - 構造体操作
 * @param person 人物情報へのポインタ
 */
void testFunction3(Person* person) {
    if (person == NULL) {
        printf("エラー: testFunction3に NULL ポインタが渡されました\n");
        return;
    }
    
    printf("testFunction3が呼び出されました\n");
    printf("名前: %s, 年齢: %d歳\n", person->name, person->age);
    
    // 年齢を1歳増加
    person->age++;
    printf("%sさんの年齢が%d歳になりました\n", person->name, person->age);
}

/**
 * @brief 配列の要素を表示するテスト用関数
 * @param arr 整数配列
 * @param size 配列のサイズ
 */
void testFunction4(int arr[], int size) {
    if (arr == NULL) {
        printf("エラー: testFunction4に NULL ポインタが渡されました\n");
        return;
    }
    
    if (size <= 0) {
        printf("エラー: 配列サイズが無効です: %d\n", size);
        return;
    }
    
    printf("testFunction4が呼び出されました: 配列サイズ=%d\n", size);
    printf("配列の内容: ");
    for (int i = 0; i < size; i++) {
        printf("%d ", arr[i]);
    }
    printf("\n");
}

/**
 * @brief 人物情報を初期化する関数
 * @param person 人物情報へのポインタ
 * @param name 名前
 * @param age 年齢
 * @return 成功時0、失敗時-1
 */
int initializePerson(Person* person, const char* name, int age) {
    if (person == NULL) {
        printf("エラー: initializePerson - person が NULL です\n");
        return -1;
    }
    
    if (name == NULL) {
        printf("エラー: initializePerson - name が NULL です\n");
        return -1;
    }
    
    if (age < 0) {
        printf("エラー: initializePerson - age が無効です: %d\n", age);
        return -1;
    }
    
    if (strlen(name) >= sizeof(person->name)) {
        printf("エラー: initializePerson - 名前が長すぎます: %s\n", name);
        return -1;
    }
    
    strcpy(person->name, name);
    person->age = age;
    
    printf("人物情報を初期化しました: %s, %d歳\n", person->name, person->age);
    return 0;
}

/**
 * @brief メイン関数
 * @return プログラムの終了ステータス
 */
int main() {
    printf("Hello World!\n");
    printf("C言語プログラミング学習を開始します。\n\n");
    
    // === 基本的な関数呼び出しのテスト ===
    printf("=== 関数呼び出しのテスト ===\n");
    
    // テスト関数1の呼び出し
    int result1 = testFunction1(10, 20);
    printf("testFunction1の結果: %d\n\n", result1);
    
    // テスト関数2の呼び出し
    testFunction2("C言語の学習");
    printf("\n");
    
    // === 構造体と関数のテスト ===
    printf("=== 構造体と関数のテスト ===\n");
    
    Person person1, person2;
    
    // 人物情報の初期化
    if (initializePerson(&person1, "田中太郎", 25) != 0) {
        printf("person1の初期化に失敗しました\n");
        return 1;
    }
    
    if (initializePerson(&person2, "佐藤花子", 30) != 0) {
        printf("person2の初期化に失敗しました\n");
        return 1;
    }
    
    printf("\n");
    
    // テスト関数3の呼び出し（構造体操作）
    testFunction3(&person1);
    testFunction3(&person2);
    printf("\n");
    
    // === 配列操作のテスト ===
    printf("=== 配列操作のテスト ===\n");
    
    int numbers[] = {1, 2, 3, 4, 5, 10, 15, 20};
    int arraySize = sizeof(numbers) / sizeof(numbers[0]);
    
    testFunction4(numbers, arraySize);
    printf("\n");
    
    // === エラーハンドリングのテスト ===
    printf("=== エラーハンドリングのテスト ===\n");
    
    // NULL ポインタのテスト
    testFunction2(NULL);
    testFunction3(NULL);
    testFunction4(NULL, 5);
    
    // 無効なサイズのテスト
    testFunction4(numbers, 0);
    testFunction4(numbers, -1);
    
    printf("\n");
    printf("プログラムが正常に終了しました。\n");
    
    return 0;
}