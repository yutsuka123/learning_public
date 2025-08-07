# Java 開発環境インストールガイド

## 📋 概要
Java開発のための環境セットアップ手順を説明します。最新のJava機能、デザインパターン、企業レベルのアプリケーション開発に適した環境を構築します。

## ✅ 前提条件
- Windows 10/11 または macOS、Linux
- 管理者権限でのコマンド実行が可能
- インターネット接続
- 4GB以上の空きディスク容量

## 🎯 インストール対象
- Java Development Kit (JDK) 17以降
- Maven または Gradle（ビルドツール）
- Visual Studio Code + Java 拡張機能
- IntelliJ IDEA（オプション）

---

## ☕ Java 環境のセットアップ

### **1. JDK のインストール状況確認**

#### **現在の状況**
```bash
java -version
javac -version
```
✅ **既にインストール済み**: Java 24.0.1

#### **JAVA_HOME 環境変数の設定**

##### **Windows**
```powershell
# 現在の設定確認
echo $env:JAVA_HOME

# 環境変数の設定（PowerShell）
$env:JAVA_HOME = "C:\Program Files\Java\jdk-24"
$env:PATH += ";$env:JAVA_HOME\bin"

# 永続的な設定（システム環境変数）
[Environment]::SetEnvironmentVariable("JAVA_HOME", "C:\Program Files\Java\jdk-24", "Machine")
```

##### **macOS/Linux**
```bash
# 現在の設定確認
echo $JAVA_HOME

# 環境変数の設定（~/.bashrc または ~/.zshrc に追加）
export JAVA_HOME=/usr/lib/jvm/java-24-openjdk
export PATH=$JAVA_HOME/bin:$PATH

# 設定の反映
source ~/.bashrc  # または source ~/.zshrc
```

### **2. ビルドツールのインストール**

#### **Maven のインストール**
```bash
# Windows (Chocolatey)
choco install maven

# macOS (Homebrew)
brew install maven

# Linux (Ubuntu/Debian)
sudo apt install maven

# 確認
mvn -version
```

#### **Gradle のインストール（オプション）**
```bash
# Windows (Chocolatey)
choco install gradle

# macOS (Homebrew)
brew install gradle

# Linux
sudo apt install gradle

# 確認
gradle -version
```

### **3. Visual Studio Code 拡張機能**

#### **必須拡張機能**
```bash
# Extension Pack for Java（Microsoft公式）
code --install-extension vscjava.vscode-java-pack

# 含まれる拡張機能:
# - Language Support for Java(TM) by Red Hat
# - Debugger for Java
# - Test Runner for Java
# - Maven for Java
# - Project Manager for Java
# - Visual Studio IntelliCode
```

#### **推奨拡張機能**
```bash
# Spring Boot Extension Pack
code --install-extension vmware.vscode-spring-boot

# Gradle for Java
code --install-extension vscjava.vscode-gradle

# Error Lens
code --install-extension usernamehw.errorlens

# SonarLint（コード品質）
code --install-extension sonarsource.sonarlint-vscode
```

---

## 🏗️ プロジェクトの設定

### **4. Maven プロジェクトの作成**

#### **新しいMavenプロジェクトの作成**
```bash
# プロジェクトディレクトリに移動
cd java

# Maven プロジェクトの作成
mvn archetype:generate -DgroupId=com.example.learning \
    -DartifactId=java-learning \
    -DarchetypeArtifactId=maven-archetype-quickstart \
    -DinteractiveMode=false

cd java-learning
```

#### **pom.xml の設定**
```xml
<?xml version="1.0" encoding="UTF-8"?>
<project xmlns="http://maven.apache.org/POM/4.0.0"
         xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
         xsi:schemaLocation="http://maven.apache.org/POM/4.0.0 
         http://maven.apache.org/xsd/maven-4.0.0.xsd">
    <modelVersion>4.0.0</modelVersion>

    <groupId>com.example.learning</groupId>
    <artifactId>java-learning</artifactId>
    <version>1.0-SNAPSHOT</version>
    <packaging>jar</packaging>

    <name>Java Learning Project</name>
    <description>Java学習用プロジェクト</description>

    <properties>
        <maven.compiler.source>17</maven.compiler.source>
        <maven.compiler.target>17</maven.compiler.target>
        <project.build.sourceEncoding>UTF-8</project.build.sourceEncoding>
        <junit.version>5.9.2</junit.version>
    </properties>

    <dependencies>
        <!-- JUnit 5 -->
        <dependency>
            <groupId>org.junit.jupiter</groupId>
            <artifactId>junit-jupiter</artifactId>
            <version>${junit.version}</version>
            <scope>test</scope>
        </dependency>

        <!-- AssertJ（テスト用アサーション）-->
        <dependency>
            <groupId>org.assertj</groupId>
            <artifactId>assertj-core</artifactId>
            <version>3.24.2</version>
            <scope>test</scope>
        </dependency>

        <!-- Mockito（モックライブラリ）-->
        <dependency>
            <groupId>org.mockito</groupId>
            <artifactId>mockito-core</artifactId>
            <version>5.1.1</version>
            <scope>test</scope>
        </dependency>

        <!-- SLF4J + Logback（ログ出力）-->
        <dependency>
            <groupId>org.slf4j</groupId>
            <artifactId>slf4j-api</artifactId>
            <version>2.0.6</version>
        </dependency>
        <dependency>
            <groupId>ch.qos.logback</groupId>
            <artifactId>logback-classic</artifactId>
            <version>1.4.5</version>
        </dependency>

        <!-- Jackson（JSON処理）-->
        <dependency>
            <groupId>com.fasterxml.jackson.core</groupId>
            <artifactId>jackson-databind</artifactId>
            <version>2.14.2</version>
        </dependency>

        <!-- Apache Commons（ユーティリティ）-->
        <dependency>
            <groupId>org.apache.commons</groupId>
            <artifactId>commons-lang3</artifactId>
            <version>3.12.0</version>
        </dependency>
    </dependencies>

    <build>
        <plugins>
            <!-- Maven Compiler Plugin -->
            <plugin>
                <groupId>org.apache.maven.plugins</groupId>
                <artifactId>maven-compiler-plugin</artifactId>
                <version>3.10.1</version>
                <configuration>
                    <source>17</source>
                    <target>17</target>
                    <encoding>UTF-8</encoding>
                </configuration>
            </plugin>

            <!-- Maven Surefire Plugin（テスト実行）-->
            <plugin>
                <groupId>org.apache.maven.plugins</groupId>
                <artifactId>maven-surefire-plugin</artifactId>
                <version>3.0.0-M9</version>
            </plugin>

            <!-- SpotBugs（静的解析）-->
            <plugin>
                <groupId>com.github.spotbugs</groupId>
                <artifactId>spotbugs-maven-plugin</artifactId>
                <version>4.7.3.0</version>
            </plugin>

            <!-- Checkstyle（コーディング規約チェック）-->
            <plugin>
                <groupId>org.apache.maven.plugins</groupId>
                <artifactId>maven-checkstyle-plugin</artifactId>
                <version>3.2.1</version>
                <configuration>
                    <configLocation>checkstyle.xml</configLocation>
                </configuration>
            </plugin>
        </plugins>
    </build>
</project>
```

### **5. Gradle プロジェクトの設定（オプション）**

#### **build.gradle**
```gradle
plugins {
    id 'java'
    id 'application'
    id 'checkstyle'
    id 'com.github.spotbugs' version '5.0.13'
}

group = 'com.example.learning'
version = '1.0-SNAPSHOT'
sourceCompatibility = '17'

repositories {
    mavenCentral()
}

dependencies {
    // JUnit 5
    testImplementation 'org.junit.jupiter:junit-jupiter:5.9.2'
    
    // AssertJ
    testImplementation 'org.assertj:assertj-core:3.24.2'
    
    // Mockito
    testImplementation 'org.mockito:mockito-core:5.1.1'
    
    // SLF4J + Logback
    implementation 'org.slf4j:slf4j-api:2.0.6'
    implementation 'ch.qos.logback:logback-classic:1.4.5'
    
    // Jackson
    implementation 'com.fasterxml.jackson.core:jackson-databind:2.14.2'
    
    // Apache Commons
    implementation 'org.apache.commons:commons-lang3:3.12.0'
}

application {
    mainClass = 'com.example.learning.HelloWorld'
}

test {
    useJUnitPlatform()
}

java {
    toolchain {
        languageVersion = JavaLanguageVersion.of(17)
    }
}
```

### **6. VS Code の設定ファイル**

#### **.vscode/settings.json**
```json
{
    "java.home": "C:\\Program Files\\Java\\jdk-24",
    "java.configuration.runtimes": [
        {
            "name": "JavaSE-17",
            "path": "C:\\Program Files\\Java\\jdk-24",
            "default": true
        }
    ],
    "java.compile.nullAnalysis.mode": "automatic",
    "java.format.settings.url": "eclipse-formatter.xml",
    "java.checkstyle.configuration": "checkstyle.xml",
    "java.test.defaultConfig": "default",
    "java.saveActions.organizeImports": true,
    "java.sources.organizeImports.starThreshold": 99,
    "java.sources.organizeImports.staticStarThreshold": 99,
    "[java]": {
        "editor.defaultFormatter": "redhat.java",
        "editor.formatOnSave": true
    }
}
```

#### **.vscode/launch.json**
```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "type": "java",
            "name": "Current File",
            "request": "launch",
            "mainClass": "${file}"
        },
        {
            "type": "java",
            "name": "HelloWorld",
            "request": "launch",
            "mainClass": "HelloWorld",
            "projectName": "java-learning"
        }
    ]
}
```

---

## 🧪 動作確認

### **7. サンプルプロジェクトのビルドと実行**

#### **直接コンパイル・実行**
```bash
# プロジェクトディレクトリに移動
cd java

# コンパイル
javac HelloWorld.java

# 実行
java HelloWorld
```

#### **Maven を使用**
```bash
# プロジェクトディレクトリに移動
cd java/java-learning

# 依存関係のダウンロード
mvn dependency:resolve

# コンパイル
mvn compile

# テスト実行
mvn test

# パッケージ化
mvn package

# 実行
java -cp target/classes com.example.learning.HelloWorld
```

#### **Gradle を使用**
```bash
# 依存関係のダウンロード
gradle build

# 実行
gradle run

# テスト実行
gradle test
```

### **8. JUnit テストの作成と実行**

#### **テストクラスの作成**
```java
// src/test/java/PersonTest.java
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.BeforeEach;
import static org.assertj.core.api.Assertions.*;

public class PersonTest {
    private Person person;

    @BeforeEach
    void setUp() {
        person = new Person("テスト太郎", 25);
    }

    @Test
    void testIntroduce() {
        String result = person.introduce();
        assertThat(result).contains("テスト太郎");
        assertThat(result).contains("25歳");
    }

    @Test
    void testIncrementAge() {
        int initialAge = person.getAge();
        person.incrementAge();
        assertThat(person.getAge()).isEqualTo(initialAge + 1);
    }

    @Test
    void testInvalidAge() {
        assertThatThrownBy(() -> new Person("テスト", -1))
            .isInstanceOf(IllegalArgumentException.class)
            .hasMessageContaining("年齢は0以上");
    }
}
```

---

## 🔍 トラブルシューティング

### **よくある問題と解決方法**

#### **JAVA_HOME が設定されていない**
```bash
# 現在の設定確認
echo $JAVA_HOME

# Javaのインストール場所確認
# Windows
where java

# macOS/Linux
which java
readlink -f $(which java)
```

#### **VS Code でJavaプロジェクトが認識されない**
1. Ctrl+Shift+P でコマンドパレット
2. "Java: Reload Projects" を実行
3. "Java: Restart Language Server" を実行

#### **Maven 依存関係の問題**
```bash
# ローカルリポジトリのクリア
mvn dependency:purge-local-repository

# 依存関係の再ダウンロード
mvn clean install -U
```

#### **文字化け問題**
```bash
# コンパイル時の文字エンコーディング指定
javac -encoding UTF-8 HelloWorld.java

# 実行時の文字エンコーディング指定
java -Dfile.encoding=UTF-8 HelloWorld
```

#### **メモリ不足エラー**
```bash
# ヒープサイズの増加
java -Xms512m -Xmx2g HelloWorld

# Maven でのメモリ設定
export MAVEN_OPTS="-Xms512m -Xmx2g"
```

---

## 📊 コード品質管理

### **9. 静的解析ツールの設定**

#### **Checkstyle 設定ファイル**
```xml
<!-- checkstyle.xml -->
<?xml version="1.0"?>
<!DOCTYPE module PUBLIC
    "-//Checkstyle//DTD Checkstyle Configuration 1.3//EN"
    "https://checkstyle.org/dtds/configuration_1_3.dtd">

<module name="Checker">
    <property name="charset" value="UTF-8"/>
    <property name="severity" value="warning"/>

    <module name="TreeWalker">
        <module name="OuterTypeFilename"/>
        <module name="IllegalTokenText"/>
        <module name="AvoidEscapedUnicodeCharacters"/>
        <module name="LineLength">
            <property name="max" value="120"/>
        </module>
        <module name="AvoidStarImport"/>
        <module name="OneTopLevelClass"/>
        <module name="NoLineWrap"/>
        <module name="EmptyBlock"/>
        <module name="NeedBraces"/>
        <module name="LeftCurly"/>
        <module name="RightCurly"/>
        <module name="WhitespaceAround"/>
        <module name="OneStatementPerLine"/>
        <module name="MultipleVariableDeclarations"/>
        <module name="ArrayTypeStyle"/>
        <module name="MissingSwitchDefault"/>
        <module name="FallThrough"/>
        <module name="UpperEll"/>
        <module name="ModifierOrder"/>
        <module name="EmptyLineSeparator"/>
        <module name="SeparatorWrap"/>
        <module name="PackageName"/>
        <module name="TypeName"/>
        <module name="MemberName"/>
        <module name="ParameterName"/>
        <module name="LocalVariableName"/>
        <module name="ClassTypeParameterName"/>
        <module name="MethodTypeParameterName"/>
        <module name="InterfaceTypeParameterName"/>
        <module name="NoFinalizer"/>
        <module name="GenericWhitespace"/>
        <module name="Indentation"/>
        <module name="MethodParamPad"/>
        <module name="ParenPad"/>
        <module name="OperatorWrap"/>
        <module name="AnnotationLocation"/>
        <module name="NonEmptyAtclauseDescription"/>
        <module name="JavadocMethod"/>
        <module name="JavadocType"/>
        <module name="JavadocVariable"/>
        <module name="JavadocStyle"/>
        <module name="AtclauseOrder"/>
    </module>
</module>
```

#### **実行コマンド**
```bash
# Maven
mvn checkstyle:check

# Gradle
gradle checkstyleMain

# SpotBugs実行
mvn spotbugs:check
```

---

## 📚 参考リンク

- [Oracle Java Documentation](https://docs.oracle.com/en/java/)
- [Maven 公式ドキュメント](https://maven.apache.org/guides/)
- [Gradle 公式ドキュメント](https://docs.gradle.org/)
- [JUnit 5 ユーザーガイド](https://junit.org/junit5/docs/current/user-guide/)
- [Spring Framework](https://spring.io/projects/spring-framework)

---

## 📝 インストール完了チェックリスト

- [ ] JDK 17以降がインストール済み
- [ ] JAVA_HOME 環境変数が設定済み
- [ ] Maven または Gradle がインストール済み
- [ ] VS Code + Java 拡張機能がインストール済み
- [ ] HelloWorld.java が正常にコンパイル・実行できる
- [ ] Maven/Gradle プロジェクトが作成できる
- [ ] JUnit テストが実行できる
- [ ] 静的解析ツールが動作する

**✅ すべて完了したら、企業レベルのJavaアプリケーション開発を開始できます！**