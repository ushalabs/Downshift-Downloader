#pragma once

#include "core/Runtime.h"
#include <QMainWindow>

namespace downshift {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(const DependencyInfo& dependencies, QWidget* parent = nullptr);
};

} // namespace downshift
