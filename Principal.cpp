#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio/videoio.hpp>

using namespace std;
using namespace cv;

Mat frame;
Mat comic;
Mat tabla(1, 256, CV_8UC1);
Mat gris;
Mat imgGauss;
Mat gx;
Mat gy;
Mat gxpos;
Mat gypos;
Mat gradiente;
Mat binaria;
Mat hsv;
Mat canales[3];
Mat moverazul = (Mat_<double>(2, 3) << 1, 0, 1, 0, 1, 0);
Mat moverRojo = (Mat_<double>(2, 3) << 1, 0, -1, 0, 1, 0);
Mat canalesGlich[3];

int main() {
    // Abrir la cámara predeterminada
    VideoCapture video(0);

    if (video.isOpened()) {
        cout << "Camara abierta correctamente" << endl;

        namedWindow("Video", WINDOW_AUTOSIZE);
        namedWindow("Comic", WINDOW_AUTOSIZE);
        namedWindow("Bordes", WINDOW_AUTOSIZE);

        // Crear la tabla LUT para reducir los niveles de intensidad
        for (int i = 0; i < 256; i++) {
            tabla.at<uchar>(0, i) = cvRound(i / 51.0) * 51;
        }

        // Capturar y procesar los fotogramas
        while (true) {
            video >> frame;

            if (frame.empty()) {
                cerr << "No se pudo obtener un fotograma" << endl;
                break;
            }

            // Convertir a escala de grises y suavizar la imagen
            cvtColor(frame, gris, COLOR_BGR2GRAY);
            GaussianBlur(gris, imgGauss, Size(5, 5), 1.73, 1.73);

            // Detectar bordes mediante el operador Sobel
            Sobel(imgGauss, gx, CV_16S, 1, 0, 3, 1, 0);
            Sobel(imgGauss, gy, CV_16S, 0, 1, 3, 1, 0);

            convertScaleAbs(gx, gxpos);
            convertScaleAbs(gy, gypos);

            // Combinar los gradientes horizontal y vertical
            addWeighted(gxpos, 0.5, gypos, 0.5, 0, gradiente);
            threshold(gradiente, binaria, 40, 255, THRESH_BINARY);

            // Reducir los niveles de color mediante LUT
            LUT(frame, tabla, comic);

            // Crear una trama de puntos negros para simular una impresión de cómic
            for (int i = 4; i < comic.rows; i += 8) {
                for (int j = 4; j < comic.cols; j += 8) {
                    int brillo = gris.at<uchar>(i, j);
                    int radio = cvRound((255 - brillo) * 3.0 / 255);

                    if (radio > 0) {
                        circle(comic, Point(j, i), radio, Scalar(0, 0, 0), FILLED);
                    }
                }
            }

            comic.setTo(Scalar(0, 0, 0), binaria);
            cvtColor(comic, hsv, COLOR_BGR2HSV);

            for (int i = 0; i < hsv.rows; i++) {
                for (int j = 0; j < hsv.cols; j++) {
                    Vec3b pixel = hsv.at<Vec3b>(i, j);

                    if (pixel[0] >= 90 && pixel[0] <= 170 && pixel[1] > 50) {
                        pixel[2] = saturate_cast<uchar>(pixel[2] * 1.15);
                    }

                    pixel[1] = saturate_cast<uchar>(pixel[1] * 1.25);
                    pixel[2] = saturate_cast<uchar>(pixel[2] * 1.05);
                    hsv.at<Vec3b>(i, j) = pixel;
                }
            }

            cvtColor(hsv, comic, COLOR_HSV2BGR);
            split(comic, canales);

            warpAffine(canales[0], canalesGlich[0], moverazul, comic.size(), INTER_NEAREST,
                       BORDER_REPLICATE);
            warpAffine(canales[2], canalesGlich[2], moverRojo, comic.size(), INTER_NEAREST,
                       BORDER_REPLICATE);

            canales[1].copyTo(canalesGlich[1]);
            merge(canalesGlich, 3, comic);

            // Mostrar los resultados
            imshow("Comic", comic);
            imshow("Video", frame);
            imshow("Bordes", gradiente);
            imshow("Binaria", binaria);

            // Salir al presionar ESC
            if (waitKey(23) == 27) {
                break;
            }
        }

        video.release();
        destroyAllWindows();

    } else {
        cerr << "No se pudo abrir la camara" << endl;
        return 1;
    }

    return 0;
}