# Kotlin 開発環境インストールガイド

## 📋 概要
Kotlin開発のための環境セットアップ手順を説明します。JVM、Android、マルチプラットフォーム開発に対応した現代的な開発環境を構築します。

## ✅ 前提条件
- Windows 10/11 または macOS、Linux
- Java 8以降がインストール済み
- 管理者権限でのコマンド実行が可能
- インターネット接続
- 4GB以上の空きディスク容量

## 🎯 インストール対象
- Kotlin Compiler
- IntelliJ IDEA（推奨）または Visual Studio Code
- Gradle（ビルドツール）
- Kotlin/JVM、Kotlin/JS、Kotlin Multiplatform 対応

---

## 🚀 Kotlin 環境のセットアップ

### **1. Java 環境の確認**
```bash
# Java バージョン確認
java -version
javac -version

# JAVA_HOME 確認
echo $JAVA_HOME  # Linux/macOS
echo $env:JAVA_HOME  # Windows PowerShell
```

### **2. Kotlin Compiler のインストール**

#### **Windows の場合**
```powershell
# Chocolateyを使用
choco install kotlin

# Scoop を使用
scoop install kotlin

# 手動インストール
# https://github.com/JetBrains/kotlin/releases/latest
# から kotlin-compiler-*.zip をダウンロード・展開
```

#### **macOS の場合**
```bash
# Homebrew を使用
brew install kotlin

# MacPorts を使用
sudo port install kotlin
```

#### **Linux の場合**
```bash
# Snapを使用（Ubuntu）
sudo snap install --classic kotlin

# 手動インストール
curl -s https://get.sdkman.io | bash
source "$HOME/.sdkman/bin/sdkman-init.sh"
sdk install kotlin
```

#### **確認**
```bash
kotlin -version
kotlinc -version
```

### **3. 開発環境の選択**

#### **オプション A: IntelliJ IDEA（推奨）**

##### **IntelliJ IDEA のインストール**
```bash
# Windows (Chocolatey)
choco install intellijidea-community

# macOS (Homebrew)
brew install --cask intellij-idea-ce

# 手動インストール
# https://www.jetbrains.com/idea/download/ からダウンロード
```

##### **必要なプラグイン**
- Kotlin（通常はプリインストール）
- Gradle
- Git Integration

#### **オプション B: Visual Studio Code**

##### **VS Code 拡張機能**
```bash
# Kotlin Language Support
code --install-extension mathiasfrohlich.kotlin

# Gradle for Java
code --install-extension vscjava.vscode-gradle

# Code Runner
code --install-extension formulahendry.code-runner
```

### **4. Gradle のインストール**

#### **Gradle のインストール**
```bash
# Windows (Chocolatey)
choco install gradle

# macOS (Homebrew)
brew install gradle

# Linux
sudo apt install gradle  # Ubuntu/Debian
sudo dnf install gradle  # Fedora

# 確認
gradle -version
```

---

## 🏗️ プロジェクトの設定

### **5. Gradle プロジェクトの作成**

#### **新しいKotlinプロジェクトの作成**
```bash
# プロジェクトディレクトリに移動
cd kotlin

# Gradle プロジェクトの初期化
gradle init --type kotlin-application --dsl kotlin

# または手動でディレクトリ構造を作成
mkdir -p src/main/kotlin
mkdir -p src/test/kotlin
```

#### **build.gradle.kts の設定**
```kotlin
// build.gradle.kts
plugins {
    kotlin("jvm") version "1.9.21"
    application
    id("org.jetbrains.kotlin.plugin.serialization") version "1.9.21"
}

group = "com.example.learning"
version = "1.0-SNAPSHOT"

repositories {
    mavenCentral()
}

dependencies {
    // Kotlin標準ライブラリ
    implementation("org.jetbrains.kotlin:kotlin-stdlib")
    
    // Coroutines（非同期処理）
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.7.3")
    
    // Serialization（シリアライゼーション）
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.6.2")
    
    // DateTime（日時処理）
    implementation("org.jetbrains.kotlinx:kotlinx-datetime:0.5.0")
    
    // HTTP クライアント
    implementation("io.ktor:ktor-client-core:2.3.7")
    implementation("io.ktor:ktor-client-cio:2.3.7")
    
    // ログ出力
    implementation("org.slf4j:slf4j-api:2.0.9")
    implementation("ch.qos.logback:logback-classic:1.4.14")
    
    // テスト
    testImplementation("org.jetbrains.kotlin:kotlin-test")
    testImplementation("org.jetbrains.kotlin:kotlin-test-junit5")
    testImplementation("org.junit.jupiter:junit-jupiter:5.10.1")
    testImplementation("io.mockk:mockk:1.13.8")
    testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.7.3")
}

application {
    mainClass.set("com.example.learning.HelloWorldKt")
}

tasks.test {
    useJUnitPlatform()
}

kotlin {
    jvmToolchain(17)
}

tasks.withType<org.jetbrains.kotlin.gradle.tasks.KotlinCompile> {
    kotlinOptions {
        freeCompilerArgs += "-Xjsr305=strict"
        jvmTarget = "17"
    }
}
```

#### **settings.gradle.kts**
```kotlin
// settings.gradle.kts
rootProject.name = "kotlin-learning"
```

### **6. IntelliJ IDEA の設定**

#### **プロジェクトの設定**
1. IntelliJ IDEA を起動
2. "Open or Import" を選択
3. build.gradle.kts ファイルを選択
4. "Import Gradle Project" を選択
5. JVM version を 17 に設定

#### **Code Style の設定**
```kotlin
// .editorconfig
root = true

[*.{kt,kts}]
charset = utf-8
end_of_line = lf
indent_style = space
indent_size = 4
insert_final_newline = true
trim_trailing_whitespace = true
max_line_length = 120

[*.{yml,yaml}]
indent_size = 2
```

### **7. VS Code の設定（VS Code使用時）**

#### **.vscode/settings.json**
```json
{
    "kotlin.languageServer.enabled": true,
    "kotlin.debugAdapter.enabled": true,
    "java.configuration.runtimes": [
        {
            "name": "JavaSE-17",
            "path": "C:\\Program Files\\Java\\jdk-17"
        }
    ],
    "gradle.nestedProjects": true,
    "[kotlin]": {
        "editor.defaultFormatter": "mathiasfrohlich.kotlin",
        "editor.formatOnSave": true
    }
}
```

#### **.vscode/tasks.json**
```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Kotlin: Build",
            "type": "shell",
            "command": "./gradlew",
            "args": ["build"],
            "group": {
                "kind": "build",
                "isDefault": true
            }
        },
        {
            "label": "Kotlin: Run",
            "type": "shell",
            "command": "./gradlew",
            "args": ["run"],
            "group": "build"
        }
    ]
}
```

---

## 🧪 動作確認

### **8. サンプルプロジェクトのビルドと実行**

#### **Gradle を使用したビルド**
```bash
# プロジェクトディレクトリに移動
cd kotlin

# 依存関係のダウンロード
gradle build

# 実行
gradle run

# テスト実行
gradle test

# JAR ファイルの作成
gradle jar
```

#### **直接コンパイル・実行**
```bash
# Kotlinファイルのコンパイル
kotlinc HelloWorld.kt -include-runtime -d HelloWorld.jar

# 実行
java -jar HelloWorld.jar

# または直接実行
kotlin HelloWorldKt
```

### **9. Coroutines のテスト**

#### **非同期処理のサンプル**
```kotlin
// CoroutinesSample.kt
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

suspend fun main() {
    println("=== Kotlin Coroutines サンプル ===")
    
    // 基本的なCoroutine
    val job = launch {
        delay(1000)
        println("Hello from Coroutine!")
    }
    
    // async/await
    val deferred = async {
        delay(500)
        "Async result"
    }
    
    println("Waiting for coroutines...")
    job.join()
    println("Async result: ${deferred.await()}")
    
    // Flow（データストリーム）
    println("\n=== Flow サンプル ===")
    (1..5).asFlow()
        .map { it * it }
        .filter { it > 10 }
        .collect { println("Flow value: $it") }
}
```

### **10. テストの作成と実行**

#### **テストクラスの作成**
```kotlin
// src/test/kotlin/PersonTest.kt
import kotlin.test.*
import io.mockk.*

class PersonTest {
    
    @Test
    fun `should introduce correctly`() {
        val person = Person("テスト太郎", 25)
        val result = person.introduce()
        
        assertTrue(result.contains("テスト太郎"))
        assertTrue(result.contains("25歳"))
    }
    
    @Test
    fun `should increment age`() {
        val person = Person("テスト", 20)
        val initialAge = person.age
        
        person.incrementAge()
        
        assertEquals(initialAge + 1, person.age)
    }
    
    @Test
    fun `should throw exception for invalid age`() {
        assertFailsWith<IllegalArgumentException> {
            Person("テスト", -1)
        }
    }
    
    @Test
    fun `should handle nullable values`() {
        val nullableName: String? = null
        val result = processNullableString(nullableName)
        
        assertEquals("nullが渡されました", result)
    }
}
```

---

## 🔍 トラブルシューティング

### **よくある問題と解決方法**

#### **Kotlin コンパイラが見つからない**
```bash
# パスの確認
echo $PATH

# Kotlin の場所確認
which kotlin
which kotlinc

# 環境変数の設定
export PATH=$PATH:/opt/kotlin/bin
```

#### **Gradle プロジェクトが認識されない**
```bash
# Gradle Wrapper の実行権限付与（Linux/macOS）
chmod +x gradlew

# Gradle キャッシュのクリア
gradle clean
rm -rf .gradle

# 依存関係の再ダウンロード
gradle build --refresh-dependencies
```

#### **IntelliJ IDEA での Kotlin プラグインエラー**
1. File → Settings → Plugins
2. Kotlin プラグインを無効化・再有効化
3. IDE を再起動
4. File → Invalidate Caches and Restart

#### **Java バージョンの不整合**
```bash
# プロジェクトのJavaバージョン確認
gradle properties | grep -i java

# Gradle の Java バージョン設定
export JAVA_HOME=/path/to/java-17
gradle build
```

#### **メモリ不足エラー**
```bash
# Gradle のメモリ設定（gradle.properties）
org.gradle.jvmargs=-Xms512m -Xmx2g -XX:MaxMetaspaceSize=512m

# Kotlin コンパイラのメモリ設定
kotlin.daemon.jvm.options=-Xmx2g
```

---

## 🚀 高度な機能

### **11. Kotlin Multiplatform の設定**

#### **build.gradle.kts（Multiplatform）**
```kotlin
plugins {
    kotlin("multiplatform") version "1.9.21"
}

kotlin {
    jvm {
        jvmToolchain(17)
        withJava()
    }
    
    js(IR) {
        browser {
            commonWebpackConfig {
                cssSupport {
                    enabled.set(true)
                }
            }
        }
    }
    
    sourceSets {
        val commonMain by getting {
            dependencies {
                implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.7.3")
                implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.6.2")
            }
        }
        
        val commonTest by getting {
            dependencies {
                implementation(kotlin("test"))
            }
        }
        
        val jvmMain by getting
        val jvmTest by getting
        
        val jsMain by getting
        val jsTest by getting
    }
}
```

### **12. Android 開発の準備**

#### **Android Studio のインストール**
```bash
# Windows (Chocolatey)
choco install androidstudio

# macOS (Homebrew)
brew install --cask android-studio

# 手動インストール
# https://developer.android.com/studio からダウンロード
```

---

## 📚 参考リンク

- [Kotlin 公式ドキュメント](https://kotlinlang.org/docs/home.html)
- [Kotlin Coroutines ガイド](https://kotlinlang.org/docs/coroutines-guide.html)
- [IntelliJ IDEA Kotlin サポート](https://www.jetbrains.com/help/idea/kotlin.html)
- [Kotlin Multiplatform](https://kotlinlang.org/docs/multiplatform.html)
- [Android Kotlin ガイド](https://developer.android.com/kotlin)

---

## 📝 インストール完了チェックリスト

- [ ] Java 8以降がインストール済み
- [ ] Kotlin Compiler がインストール済み
- [ ] IntelliJ IDEA または VS Code + Kotlin拡張機能がインストール済み
- [ ] Gradle がインストール済み
- [ ] HelloWorld.kt が正常にコンパイル・実行できる
- [ ] Gradle プロジェクトが作成・ビルドできる
- [ ] Coroutines のサンプルが実行できる
- [ ] テストが実行できる

**✅ すべて完了したら、現代的なKotlinアプリケーション開発を開始できます！**