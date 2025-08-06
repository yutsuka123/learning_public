"""
Python Hello World プログラム
概要: Pythonの基本的な構文とオブジェクト指向プログラミングのサンプル
主な仕様:
- print文を使用したコンソール出力
- クラスとオブジェクトの基本的な使用方法
- プロパティとメソッドの実装
- 例外処理の実装
制限事項: Python 3.8以降が推奨
"""

from typing import List
import sys


class Person:
    """
    練習用のサンプルクラス
    人物の情報を管理するクラス
    """
    
    def __init__(self, name: str, age: int) -> None:
        """
        コンストラクタ
        
        Args:
            name (str): 名前
            age (int): 年齢
            
        Raises:
            ValueError: 無効な引数が渡された場合
            TypeError: 引数の型が不正な場合
        """
        try:
            if not isinstance(name, str):
                raise TypeError(f"名前は文字列である必要があります: {type(name)}")
            if not isinstance(age, int):
                raise TypeError(f"年齢は整数である必要があります: {type(age)}")
            if not name.strip():
                raise ValueError("名前が空文字列です")
            if age < 0:
                raise ValueError(f"年齢は0以上である必要があります: {age}")
                
            self._name = name.strip()
            self._age = age
            
        except (ValueError, TypeError) as e:
            print(f"エラーが発生しました - クラス: Person, メソッド: __init__, "
                  f"引数: name={name}, age={age}, エラー: {e}")
            raise

    @property
    def name(self) -> str:
        """
        名前プロパティのゲッター
        
        Returns:
            str: 名前
        """
        return self._name

    @property
    def age(self) -> int:
        """
        年齢プロパティのゲッター
        
        Returns:
            int: 年齢
        """
        return self._age

    @age.setter
    def age(self, value: int) -> None:
        """
        年齢プロパティのセッター
        
        Args:
            value (int): 新しい年齢
            
        Raises:
            ValueError: 無効な年齢が指定された場合
            TypeError: 引数の型が不正な場合
        """
        try:
            if not isinstance(value, int):
                raise TypeError(f"年齢は整数である必要があります: {type(value)}")
            if value < 0:
                raise ValueError(f"年齢は0以上である必要があります: {value}")
            self._age = value
        except (ValueError, TypeError) as e:
            print(f"エラーが発生しました - クラス: Person, メソッド: age.setter, "
                  f"引数: value={value}, エラー: {e}")
            raise

    def introduce(self) -> str:
        """
        自己紹介メソッド
        
        Returns:
            str: 自己紹介文字列
        """
        try:
            return f"こんにちは！私の名前は{self._name}で、{self._age}歳です。"
        except Exception as e:
            print(f"エラーが発生しました - クラス: Person, メソッド: introduce, エラー: {e}")
            raise

    def increment_age(self) -> None:
        """
        年齢を1歳増加させるメソッド
        """
        try:
            self._age += 1
            print(f"{self._name}さんの年齢が{self._age}歳になりました。")
        except Exception as e:
            print(f"エラーが発生しました - クラス: Person, メソッド: increment_age, エラー: {e}")
            raise

    def __str__(self) -> str:
        """
        文字列表現を返すマジックメソッド
        
        Returns:
            str: オブジェクトの文字列表現
        """
        return f"Person(name='{self._name}', age={self._age})"

    def __repr__(self) -> str:
        """
        開発者向けの文字列表現を返すマジックメソッド
        
        Returns:
            str: オブジェクトの詳細な文字列表現
        """
        return f"Person(name='{self._name}', age={self._age})"


def test_function_1(a: int, b: int) -> int:
    """
    テスト用の関数1 - 基本的な計算
    
    Args:
        a (int): 第一引数
        b (int): 第二引数
        
    Returns:
        int: a + b の結果
        
    Raises:
        TypeError: 引数の型が不正な場合
    """
    try:
        if not isinstance(a, int) or not isinstance(b, int):
            raise TypeError(f"引数は整数である必要があります: a={type(a)}, b={type(b)}")
        
        print(f"test_function_1が呼び出されました: 引数 a={a}, b={b}")
        return a + b
    except Exception as e:
        print(f"エラーが発生しました - 関数: test_function_1, 引数: a={a}, b={b}, エラー: {e}")
        raise


def test_function_2(text: str) -> None:
    """
    テスト用の関数2 - 文字列処理
    
    Args:
        text (str): 処理する文字列
        
    Raises:
        TypeError: 引数の型が不正な場合
    """
    try:
        if not isinstance(text, str):
            raise TypeError(f"引数は文字列である必要があります: {type(text)}")
        
        print(f"test_function_2が呼び出されました: 文字列 \"{text}\"")
        print(f"文字列の長さ: {len(text)}文字")
        print(f"大文字変換: {text.upper()}")
    except Exception as e:
        print(f"エラーが発生しました - 関数: test_function_2, 引数: text={text}, エラー: {e}")
        raise


def test_function_3(numbers: List[int]) -> None:
    """
    テスト用の関数3 - リスト処理
    
    Args:
        numbers (List[int]): 整数のリスト
        
    Raises:
        TypeError: 引数の型が不正な場合
        ValueError: 空のリストが渡された場合
    """
    try:
        if not isinstance(numbers, list):
            raise TypeError(f"引数はリストである必要があります: {type(numbers)}")
        
        if not numbers:
            raise ValueError("空のリストが渡されました")
        
        if not all(isinstance(num, int) for num in numbers):
            raise TypeError(
                "リストのすべての要素は整数である必要があります"
            )
        
        print(f"test_function_3が呼び出されました: リストサイズ={len(numbers)}")
        print(f"リストの内容: {numbers}")
        print(f"合計: {sum(numbers)}")
        print(f"平均: {sum(numbers) / len(numbers):.2f}")
        print(f"最大値: {max(numbers)}")
        print(f"最小値: {min(numbers)}")
    except Exception as e:
        print(f"エラーが発生しました - 関数: test_function_3, 引数: numbers={numbers}, エラー: {e}")
        raise


def main() -> None:
    """
    メイン関数
    プログラムのエントリーポイント
    """
    try:
        # Hello World の出力
        print("Hello World!")
        print("Pythonプログラミング学習を開始します。")
        print()

        # === 基本的な関数呼び出しのテスト ===
        print("=== 関数呼び出しのテスト ===")
        
        # テスト関数1の呼び出し
        result1 = test_function_1(10, 20)
        print(f"test_function_1の結果: {result1}")
        print()
        
        # テスト関数2の呼び出し
        test_function_2("Pythonの学習")
        print()
        
        # テスト関数3の呼び出し
        numbers = [1, 2, 3, 4, 5, 10, 15, 20]
        test_function_3(numbers)
        print()

        # === オブジェクト指向プログラミングのサンプル ===
        print("=== オブジェクト指向プログラミングのサンプル ===")

        # インスタンスの作成
        person1 = Person("田中太郎", 25)
        person2 = Person("佐藤花子", 30)

        # メソッドの呼び出し
        print(person1.introduce())
        print(person2.introduce())
        print()

        # プロパティの変更とメソッド呼び出し
        print("年齢を増加させます...")
        person1.increment_age()
        person2.increment_age()
        print()

        # 変更後の状態確認
        print("変更後の情報:")
        print(person1.introduce())
        print(person2.introduce())
        print()

        # オブジェクトの文字列表現
        print("オブジェクトの文字列表現:")
        print(f"person1: {person1}")
        print(f"person2: {person2}")
        print()

        # === リスト内包表記のサンプル ===
        print("=== リスト内包表記のサンプル ===")
        people = [person1, person2]
        introductions = [person.introduce() for person in people]
        for intro in introductions:
            print(intro)
        print()

        # === エラーハンドリングのテスト ===
        print("=== エラーハンドリングのテスト ===")
        
        try:
            # 無効な引数でPersonを作成
            Person("", -5)
        except (ValueError, TypeError) as e:
            print(f"期待通りのエラーをキャッチしました: {e}")
        
        try:
            # 無効な型で関数を呼び出し
            test_function_1("文字列", 10)
        except TypeError as e:
            print(f"期待通りのエラーをキャッチしました: {e}")

        print()
        print("プログラムが正常に終了しました。")

    except Exception as e:
        print(f"予期しないエラーが発生しました - 関数: main, エラー: {e}")
        sys.exit(1)



if __name__ == "__main__":
    main()