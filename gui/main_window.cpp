#include "main_window.h"
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setupUi();
}
void MainWindow::setupUi() {
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *layout = new QVBoxLayout(central);
    layout->addWidget(new QWidget());   // заглушка
    menuBar()->addMenu("Файл");
    menuBar()->addMenu("Инструменты");
}
