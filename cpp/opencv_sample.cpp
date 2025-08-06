/**
 * OpenCV サンプルプログラム
 * 概要: OpenCVライブラリを使用した基本的な図形描画のサンプル
 * 主な仕様:
 * - 空白のキャンバス作成
 * - 基本図形（円、矩形、線、テキスト）の描画
 * - 画像の保存と表示
 * 制限事項: OpenCV 4.0以降が必要
 */

#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>

/**
 * @brief OpenCVを使用した図形描画クラス
 */
class SimpleDrawing {
private:
    cv::Mat canvas_;  ///< 描画用のキャンバス
    int width_;       ///< キャンバスの幅
    int height_;      ///< キャンバスの高さ

public:
    /**
     * @brief コンストラクタ
     * @param width キャンバスの幅
     * @param height キャンバスの高さ
     */
    SimpleDrawing(int width = 800, int height = 600) 
        : width_(width), height_(height) {
        try {
            // 白いキャンバスを作成（3チャンネル、8bit、白色で初期化）
            canvas_ = cv::Mat::zeros(height_, width_, CV_8UC3);
            canvas_.setTo(cv::Scalar(255, 255, 255)); // 白色で塗りつぶし
            
            std::cout << "キャンバス作成完了: " << width_ << "x" << height_ << std::endl;
        }
        catch (const cv::Exception& e) {
            std::cerr << "OpenCVエラーが発生しました - クラス: SimpleDrawing, メソッド: コンストラクタ"
                      << ", エラー: " << e.what() << std::endl;
            throw;
        }
        catch (const std::exception& e) {
            std::cerr << "エラーが発生しました - クラス: SimpleDrawing, メソッド: コンストラクタ"
                      << ", エラー: " << e.what() << std::endl;
            throw;
        }
    }

    /**
     * @brief 円を描画するメソッド
     * @param center 中心座標
     * @param radius 半径
     * @param color 色（BGR形式）
     * @param thickness 線の太さ（-1で塗りつぶし）
     */
    void drawCircle(const cv::Point& center, int radius, const cv::Scalar& color, int thickness = 2) {
        try {
            cv::circle(canvas_, center, radius, color, thickness);
            std::cout << "円を描画しました: 中心(" << center.x << "," << center.y 
                      << "), 半径=" << radius << std::endl;
        }
        catch (const cv::Exception& e) {
            std::cerr << "OpenCVエラーが発生しました - クラス: SimpleDrawing, メソッド: drawCircle"
                      << ", エラー: " << e.what() << std::endl;
            throw;
        }
    }

    /**
     * @brief 矩形を描画するメソッド
     * @param topLeft 左上座標
     * @param bottomRight 右下座標
     * @param color 色（BGR形式）
     * @param thickness 線の太さ（-1で塗りつぶし）
     */
    void drawRectangle(const cv::Point& topLeft, const cv::Point& bottomRight, 
                      const cv::Scalar& color, int thickness = 2) {
        try {
            cv::rectangle(canvas_, topLeft, bottomRight, color, thickness);
            std::cout << "矩形を描画しました: (" << topLeft.x << "," << topLeft.y 
                      << ") - (" << bottomRight.x << "," << bottomRight.y << ")" << std::endl;
        }
        catch (const cv::Exception& e) {
            std::cerr << "OpenCVエラーが発生しました - クラス: SimpleDrawing, メソッド: drawRectangle"
                      << ", エラー: " << e.what() << std::endl;
            throw;
        }
    }

    /**
     * @brief 線を描画するメソッド
     * @param start 開始点
     * @param end 終了点
     * @param color 色（BGR形式）
     * @param thickness 線の太さ
     */
    void drawLine(const cv::Point& start, const cv::Point& end, 
                  const cv::Scalar& color, int thickness = 2) {
        try {
            cv::line(canvas_, start, end, color, thickness);
            std::cout << "線を描画しました: (" << start.x << "," << start.y 
                      << ") - (" << end.x << "," << end.y << ")" << std::endl;
        }
        catch (const cv::Exception& e) {
            std::cerr << "OpenCVエラーが発生しました - クラス: SimpleDrawing, メソッド: drawLine"
                      << ", エラー: " << e.what() << std::endl;
            throw;
        }
    }

    /**
     * @brief テキストを描画するメソッド
     * @param text 描画するテキスト
     * @param position テキストの位置
     * @param color 色（BGR形式）
     * @param scale フォントサイズのスケール
     */
    void drawText(const std::string& text, const cv::Point& position, 
                  const cv::Scalar& color, double scale = 1.0) {
        try {
            cv::putText(canvas_, text, position, cv::FONT_HERSHEY_SIMPLEX, 
                       scale, color, 2, cv::LINE_AA);
            std::cout << "テキストを描画しました: \"" << text << "\" at (" 
                      << position.x << "," << position.y << ")" << std::endl;
        }
        catch (const cv::Exception& e) {
            std::cerr << "OpenCVエラーが発生しました - クラス: SimpleDrawing, メソッド: drawText"
                      << ", エラー: " << e.what() << std::endl;
            throw;
        }
    }

    /**
     * @brief 画像を保存するメソッド
     * @param filename 保存ファイル名
     */
    void saveImage(const std::string& filename) {
        try {
            bool success = cv::imwrite(filename, canvas_);
            if (success) {
                std::cout << "画像を保存しました: " << filename << std::endl;
            } else {
                throw std::runtime_error("画像の保存に失敗しました");
            }
        }
        catch (const cv::Exception& e) {
            std::cerr << "OpenCVエラーが発生しました - クラス: SimpleDrawing, メソッド: saveImage"
                      << ", エラー: " << e.what() << std::endl;
            throw;
        }
    }

    /**
     * @brief 画像を表示するメソッド
     * @param windowName ウィンドウ名
     */
    void showImage(const std::string& windowName = "OpenCV Sample") {
        try {
            cv::imshow(windowName, canvas_);
            std::cout << "画像を表示しました。何かキーを押すと閉じます..." << std::endl;
            cv::waitKey(0); // キー入力待ち
            cv::destroyAllWindows();
        }
        catch (const cv::Exception& e) {
            std::cerr << "OpenCVエラーが発生しました - クラス: SimpleDrawing, メソッド: showImage"
                      << ", エラー: " << e.what() << std::endl;
            throw;
        }
    }
};

/**
 * @brief メイン関数
 * @return プログラムの終了ステータス
 */
int main() {
    try {
        std::cout << "=== OpenCV サンプルプログラム ===" << std::endl;
        std::cout << "OpenCV バージョン: " << CV_VERSION << std::endl;
        std::cout << std::endl;

        // SimpleDrawingクラスのインスタンスを作成
        SimpleDrawing drawing(800, 600);

        // 様々な図形を描画
        std::cout << "図形を描画しています..." << std::endl;

        // 円を描画（青色）
        drawing.drawCircle(cv::Point(200, 150), 50, cv::Scalar(255, 0, 0), -1);

        // 矩形を描画（緑色の枠線）
        drawing.drawRectangle(cv::Point(300, 100), cv::Point(500, 200), 
                             cv::Scalar(0, 255, 0), 3);

        // 線を描画（赤色）
        drawing.drawLine(cv::Point(100, 300), cv::Point(700, 400), 
                        cv::Scalar(0, 0, 255), 4);

        // テキストを描画（黒色）
        drawing.drawText("Hello OpenCV!", cv::Point(250, 500), 
                        cv::Scalar(0, 0, 0), 2.0);

        // 複数の小さな円を描画（カラフル）
        for (int i = 0; i < 5; ++i) {
            cv::Point center(100 + i * 120, 450);
            cv::Scalar color(i * 50, 255 - i * 40, 100 + i * 30);
            drawing.drawCircle(center, 20, color, -1);
        }

        std::cout << std::endl;
        std::cout << "描画完了！" << std::endl;

        // 画像を保存
        drawing.saveImage("opencv_sample.png");

        // 画像を表示（GUI環境がある場合）
        // drawing.showImage();

        std::cout << "プログラムが正常に終了しました。" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "予期しないエラーが発生しました - メソッド: main"
                  << ", エラー: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}