"""
Webスクレイピング（requests + BeautifulSoup）と、取得データのpandas分析サンプル。

概要:
    requestsでHTMLを取得し、BeautifulSoupで解析、pandasのDataFrameに変換して
    集計するという、「Webからデータを取ってきて分析する」一連の流れを確認する。
    題材には https://quotes.toscrape.com/ を使用する。これは
    Zyte社（Scrapy開発元）がスクレイピング練習のために公式に用意しているサイトであり、
    実在の企業サイト等を無断でスクレイピングする代わりに、安全に練習できる。
主な仕様:
    - demonstrateFetchAndParse(): requestsで取得し、BeautifulSoupで名言・著者・タグを抽出。
    - demonstrateAnalyzeWithPandas(): 抽出したデータをDataFrame化し、著者別集計等を行う。
制限事項:
    - [重要] スクレイピング対象サイトの利用規約・`robots.txt`を確認し、過度な高頻度
      アクセスを避けること（本サンプルでは各リクエスト間に`time.sleep`を挟んでいる）。
      実在の企業サイトを対象にする場合は特に注意（法的・倫理的な問題になりうる）。

実行方法:
    cd python/web_scraping
    pip install requests beautifulsoup4 pandas
    python3 scraping_and_analysis.py
    # 検証環境: requests, beautifulsoup4, pandas 3.0.5 で実行結果を確認済み
    # （ネットワーク接続が必要。対象サイトの応答内容が変わると出力も変わりうる）。
"""

import time

import pandas as pd
import requests
from bs4 import BeautifulSoup

BASE_URL = "https://quotes.toscrape.com"


def fetchQuotesFromPage(pageNumber):
    """
    指定ページから名言データを取得・解析する。

    引数:
        pageNumber (int): ページ番号（1始まり）。
    戻り値:
        list[dict]: {"text": ..., "author": ..., "tags": [...]} のリスト。
    """

    url = f"{BASE_URL}/page/{pageNumber}/"
    # [重要] User-Agentを名乗るのはマナー（多くのサイトが既定のUser-Agent無しリクエストを
    # 拒否・制限することがあるため）。タイムアウトも必ず指定し、応答が返らない相手に
    # 無限に待たされないようにする。
    response = requests.get(url, headers={"User-Agent": "learning-public-sample/1.0"}, timeout=10)
    response.raise_for_status()  # 4xx/5xxなら例外を投げる（黙って空データを返さない）

    soup = BeautifulSoup(response.text, "html.parser")
    quotes = []
    for quoteDiv in soup.select(".quote"):
        text = quoteDiv.select_one(".text").get_text(strip=True)
        author = quoteDiv.select_one(".author").get_text(strip=True)
        tags = [tagEl.get_text(strip=True) for tagEl in quoteDiv.select(".tags .tag")]
        quotes.append({"text": text, "author": author, "tags": tags})

    return quotes


def demonstrateFetchAndParse():
    """
    1ページ分の名言を取得し、内容を確認する。

    実行結果（例。対象サイトのコンテンツが変わらない限り再現するはずだが、
    サイト側の更新により変わる可能性がある）:
        fetched 10 quotes from page 1
        first quote: text='"The world as we have created it is a process of our thinkin'...
        author=Albert Einstein tags=['change', 'deep-thoughts', 'thinking', 'world']
    """

    print("=== 1. fetch and parse (requests + BeautifulSoup) ===")

    quotes = fetchQuotesFromPage(1)
    print(f"fetched {len(quotes)} quotes from page 1")

    first = quotes[0]
    print(f"first quote: text={first['text'][:60]!r}...")
    print(f"author={first['author']} tags={first['tags']}")


def demonstrateAnalyzeWithPandas():
    """
    複数ページを取得し、pandasで著者別・タグ別に集計する。

    目的:
        「Webから取ってきたデータをそのまま分析に使う」流れを、複数ページ分の
        データ取得→DataFrame化→groupbyでの集計という形で確認する。

    実行結果（例。対象サイトの内容次第で変わりうる）:
        total quotes fetched: 30 (from 3 pages)
        quotes per author (top 5):
        author
        Albert Einstein    ...
        ...
        most common tags (top 5):
        ...
    """

    print("\n=== 2. analyze scraped data with pandas ===")

    allQuotes = []
    for pageNumber in range(1, 4):  # 1〜3ページ分
        allQuotes.extend(fetchQuotesFromPage(pageNumber))
        time.sleep(0.5)  # [重要] 連続リクエストの間隔をあける（相手サーバーへの配慮）

    df = pd.DataFrame(allQuotes)
    print(f"total quotes fetched: {len(df)} (from 3 pages)")

    quotesPerAuthor = df["author"].value_counts().head(5)
    print(f"quotes per author (top 5):\n{quotesPerAuthor}")

    # tags列は「1セルにリスト」が入っているため、explode()で「1タグ1行」に展開してから集計する
    allTags = df.explode("tags")["tags"]
    print(f"most common tags (top 5):\n{allTags.value_counts().head(5)}")


if __name__ == "__main__":
    demonstrateFetchAndParse()
    demonstrateAnalyzeWithPandas()
