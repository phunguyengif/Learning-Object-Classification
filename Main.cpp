#include <iostream>
#include <string>
#include <sstream>
#include <cmath>
#include <memory>
using namespace std;

// OpenCV includes
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/ml.hpp>

#include "utils/MultipleImageWindow.h"

using namespace cv;
using namespace cv::ml;

shared_ptr<MultipleImageWindow> miw;
Mat light_pattern;
Ptr<SVM> svm;
Scalar green (0, 255, 0), blue (255, 0, 0), red (0, 0, 255);
// OpenCV command line parser functions
// Keys accecpted by command line parser
const char* keys =
{
   "{help h usage ? | | print this message}"
   "{@image || Image to classify}"
};

static Scalar randomColor(RNG& rng)
{
    int icolor = (unsigned)rng;
    return Scalar(icolor & 255, (icolor >> 8) & 255, (icolor >> 16) & 255);
}


void plotTrainData(Mat trainData, Mat labels, float* error = NULL)
{
    float area_max, ar_max, area_min, ar_min;
    area_max = ar_max = 0;
    area_min = ar_min = 99999999;

    for (int i = 0; i < trainData.rows; i++) {
        float area = trainData.at<float>(i, 0);
        float ar = trainData.at<float>(i, 1);
        if (area > area_max)
            area_max = area;
        if (ar > ar_max)
            ar_max = ar;
        if (area < area_min)
            area_min = area;
        if (ar < ar_min)
            ar_min = ar;
    }

    Mat plot = Mat::zeros(512, 512, CV_8UC3);
    for (int i = 0; i < trainData.rows; i++) {
        float area = trainData.at<float>(i, 0);
        float ar = trainData.at<float>(i, 1);
        int x = (int)(512.0f * ((area - area_min) / (area_max - area_min)));
        int y = (int)(512.0f * ((ar - ar_min) / (ar_max - ar_min)));

        int label = labels.at<int>(i);

        Scalar color;
        if (label == 0)
            color = green; // NUT
        else if (label == 1)
            color = blue; // RING
        else if (label == 2)
            color = red; // SCREW

        circle(plot, Point(x, y), 3, color, -1, 8);
    }

    if (error != NULL) {
        stringstream ss;
        ss << "Error: " << *error << "\%";
        putText(plot, ss.str().c_str(), Point(20, 512 - 40), FONT_HERSHEY_SIMPLEX, 0.75, Scalar(200, 200, 200), 1, LINE_AA);
    }
    miw->addImage("Plot", plot);

}


vector< vector<float> > ExtractFeatures(Mat img, vector<int>* left = NULL, vector<int>* top = NULL)
{
    vector< vector<float> > output;
    vector<vector<Point> > contours;
    Mat input = img.clone();

    vector<Vec4i> hierarchy;
    findContours(input, contours, hierarchy, RETR_CCOMP, CHAIN_APPROX_SIMPLE);

    if (contours.size() == 0) {
        return output;
    }
    RNG rng(0xFFFFFFFF);
    for (int i = 0; i < contours.size(); i++) {

        Mat mask = Mat::zeros(img.rows, img.cols, CV_8UC1);
        drawContours(mask, contours, i, Scalar(1), FILLED, LINE_8, hierarchy, 1);
        Scalar area_s = sum(mask);
        float area = area_s[0];

        if (area > 500) { 

            RotatedRect r = minAreaRect(contours[i]);
            float width = r.size.width;
            float height = r.size.height;
            float ar = (width < height) ? height / width : width / height;

            vector<float> row;
            row.push_back(area);
            row.push_back(ar);
            output.push_back(row);
            if (left != NULL) {
                left->push_back((int)r.center.x);
            }
            if (top != NULL) {
                top->push_back((int)r.center.y);
            }

            miw->addImage("Extract Features", mask * 255);
            miw->render();
            waitKey(10);
        }
    }
    return output;
}

Mat removeLight(Mat img, Mat pattern)
{
    Mat aux;

    
    Mat img32, pattern32;
    img.convertTo(img32, CV_32F);
    pattern.convertTo(pattern32, CV_32F);
    
    aux = 1 - (img32 / pattern32);
    aux = aux * 255;

    aux.convertTo(aux, CV_8U);

    return aux;
}

Mat preprocessImage(Mat input)
{
    if (input.channels() == 3)
        cvtColor(input, input, COLOR_RGB2GRAY);
    Mat result;
    Mat img_noise, img_box_smooth;
    medianBlur(input, img_noise, 3);


    Mat img_no_light;
    img_noise.copyTo(img_no_light);
    img_no_light = removeLight(img_noise, light_pattern);

    threshold(img_no_light, result, 30, 255, THRESH_BINARY);

    return result;
}

bool readFolderAndExtractFeatures(string folder, int label, int num_for_test,
    vector<float>& trainingData, vector<int>& responsesData,
    vector<float>& testData, vector<float>& testResponsesData)
{
    VideoCapture images;
    if (images.open(folder) == false) {
        cout << "Can not open the folder images" << endl;
        return false;
    }
    Mat frame;
    int img_index = 0;
    while (images.read(frame)) {

        Mat pre = preprocessImage(frame);
        vector< vector<float> > features = ExtractFeatures(pre);
        for (int i = 0; i < features.size(); i++) {
            if (img_index >= num_for_test) {
                trainingData.push_back(features[i][0]);
                trainingData.push_back(features[i][1]);
                responsesData.push_back(label);
            }
            else {
                testData.push_back(features[i][0]);
                testData.push_back(features[i][1]);
                testResponsesData.push_back((float)label);
            }
        }
        img_index++;
    }
    return true;
}

void trainAndTest()
{
    vector< float > trainingData;
    vector< int > responsesData;
    vector< float > testData;
    vector< float > testResponsesData;

    int num_for_test = 20;

    // Get the nut images
    readFolderAndExtractFeatures("data\\nut\\tuerca_%04d.pgm", 0, num_for_test, trainingData, responsesData, testData, testResponsesData);
    // Get and process the ring images
    readFolderAndExtractFeatures("data\\ring\\arandela_%04d.pgm", 1, num_for_test, trainingData, responsesData, testData, testResponsesData);
    // get and process the screw images
    readFolderAndExtractFeatures("data\\screw\\tornillo_%04d.pgm", 2, num_for_test, trainingData, responsesData, testData, testResponsesData);

    cout << "Num of train samples: " << responsesData.size() << endl;

    cout << "Num of test samples: " << testResponsesData.size() << endl;


    Mat trainingDataMat(trainingData.size() / 2, 2, CV_32FC1, &trainingData[0]);
    Mat responses(responsesData.size(), 1, CV_32SC1, &responsesData[0]);

    Mat testDataMat(testData.size() / 2, 2, CV_32FC1, &testData[0]);
    Mat testResponses(testResponsesData.size(), 1, CV_32FC1, &testResponsesData[0]);

    Ptr<TrainData> tdata = TrainData::create(trainingDataMat, ROW_SAMPLE, responses);

    svm = cv::ml::SVM::create();
    svm->setType(cv::ml::SVM::C_SVC);
    svm->setNu(0.05);
    svm->setKernel(cv::ml::SVM::CHI2);
    svm->setDegree(1.0);
    svm->setGamma(2.0);
    svm->setTermCriteria(TermCriteria(TermCriteria::MAX_ITER, 100, 1e-6));
    svm->train(tdata);

    if (testResponsesData.size() > 0) {
        cout << "Evaluation" << endl;
        cout << "==========" << endl;
        // Test the ML Model
        Mat testPredict;
        svm->predict(testDataMat, testPredict);
        cout << "Prediction Done" << endl;
        // Error calculation
        Mat errorMat = testPredict != testResponses;
        float error = 100.0f * countNonZero(errorMat) / testResponsesData.size();
        cout << "Error: " << error << "\%" << endl;
        // Plot training data with error label
        plotTrainData(trainingDataMat, responses, &error);

    }
    else {
        plotTrainData(trainingDataMat, responses);
    }
}

int main(int argc, const char** argv)
{
    CommandLineParser parser(argc, argv, keys);
    parser.about("Chapter 6. Classification v1.0.0");
 
    if (parser.has("help"))
    {
        parser.printMessage();
        return 0;
    }

    String img_file = parser.get<String>(0);
    String light_pattern_file = "data\\pattern.pgm";


    if (!parser.check())
    {
        parser.printErrors();
        return 0;
    }

    // Create the Multiple Image Window
    miw = make_shared<MultipleImageWindow>("Main window", 2, 2, WINDOW_AUTOSIZE);


    // Load image to process
    Mat img = imread("data\\test.pgm", 0);
    if (img.data == NULL) {
        cout << "Error loading image " << img_file << endl;
        return 0;
    }
    Mat img_output = img.clone();
    cvtColor(img_output, img_output, COLOR_GRAY2BGR);

    // Load image to process
    light_pattern = imread(light_pattern_file, 0);
    if (light_pattern.data == NULL) {
        cout << "ERROR: Not light patter loaded" << endl;
        return 0;
    }
    medianBlur(light_pattern, light_pattern, 3);

    trainAndTest();

    //// Preprocess image
    Mat pre = preprocessImage(img);
    ////End preprocess

    // Extract features
    vector<int> pos_top, pos_left;
    vector< vector<float> > features = ExtractFeatures(pre, &pos_left, &pos_top);

    cout << "Num objects extracted features " << features.size() << endl;

    for (int i = 0; i < features.size(); i++) {

        cout << "Data Area AR: " << features[i][0] << " " << features[i][1] << endl;

        Mat trainingDataMat(1, 2, CV_32FC1, &features[i][0]);
        cout << "Features to predict: " << trainingDataMat << endl;
        float result = svm->predict(trainingDataMat);
        cout << result << endl;


        stringstream ss;
        Scalar color;
        if (result == 0) {
            color = green; // NUT
            ss << "NUT";
        }
        else if (result == 1) {
            color = blue; // RING
            ss << "RING";
        }
        else if (result == 2) {
            color = red; // SCREW
            ss << "SCREW";
        }

        putText(img_output,
            ss.str(),
            Point2d(pos_left[i], pos_top[i]),
            FONT_HERSHEY_SIMPLEX,
            0.4,
            color);

    }

 /*   vector<int> results= evaluate(features);*/

    // Show images
    miw->addImage("Binary image", pre);
    miw->addImage("Result", img_output);
    miw->render();
    waitKey(0);



    return 0;

}