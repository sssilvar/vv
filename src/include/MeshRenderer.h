#pragma once
#include <vector>
#include <string>
#include <array>
#include <vtkSmartPointer.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkScalarBarActor.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>

class MeshRenderer
{
public:
    MeshRenderer();
    ~MeshRenderer();
    void setup(const std::vector<vtkSmartPointer<vtkPolyData>> &polys,
               const std::vector<std::string> &names,
               const std::vector<std::array<double, 3>> &colorsHex);
    void start();

private:
    vtkSmartPointer<vtkRenderer> renderer;
    vtkSmartPointer<vtkRenderWindow> window;
    vtkSmartPointer<vtkRenderWindowInteractor> interactor;
    vtkSmartPointer<vtkScalarBarActor> bar;
    std::vector<vtkSmartPointer<vtkPolyDataMapper>> mappers;
    std::vector<vtkSmartPointer<vtkActor>> actors;
};
