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

struct RendererContext {
    vtkSmartPointer<vtkRenderWindow> window;
    vtkSmartPointer<vtkScalarBarActor> bar;
    std::vector<vtkSmartPointer<vtkActor>> actors;
    std::vector<std::array<double, 3>> colorsHex;
};

class MeshRenderer
{
public:
    MeshRenderer();
    ~MeshRenderer();
    void setup(const std::vector<vtkSmartPointer<vtkPolyData>> &polys,
               const std::vector<std::string> &names,
               const std::vector<std::array<double, 3>> &colorsHex);
    void start();

    void setupFacetGrid(const std::vector<vtkSmartPointer<vtkPolyData>>& polys,
                       const std::vector<std::string>& names,
                       const std::vector<std::array<double, 3>>& colorsHex);
    void startFacetGrid();

    RendererContext context;
private:
    vtkSmartPointer<vtkRenderer> renderer;
    vtkSmartPointer<vtkRenderWindowInteractor> interactor;
    std::vector<vtkSmartPointer<vtkPolyDataMapper>> mappers;
};
