#pragma once
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyData.h>
#include <vtkRenderWindow.h>
#include <vtkScalarBarActor.h>
#include <vector>
#include <string>
#include <vtkActor.h>
#include <array>
#include "MeshRenderer.h"

class ScalarCyclerStyle : public vtkInteractorStyleTrackballCamera
{
public:
    static ScalarCyclerStyle *New();
    vtkTypeMacro(ScalarCyclerStyle, vtkInteractorStyleTrackballCamera);
    void initialize(const RendererContext& ctx,
                    const std::vector<std::string>& names,
                    const std::vector<vtkPolyDataMapper *>& mappers,
                    const std::vector<vtkPolyData *>& polys);
    void UpdateColorbar(const std::string &name, bool show = true);
    void UpdateColorbar(const std::string &name, bool show, vtkPolyData *poly, vtkPolyDataMapper *mapper);
    void OnKeyPress() override;
    void SetActors(const std::vector<vtkActor *> &actors, const std::vector<std::array<double, 3>> &baseColors);

private:
    vtkSmartPointer<vtkRenderWindow> window;
    vtkSmartPointer<vtkScalarBarActor> bar;
    std::vector<vtkSmartPointer<vtkActor>> actors;
    std::vector<std::array<double, 3>> baseColors;
    std::vector<std::string> scalarNames;
    std::vector<vtkPolyDataMapper *> mappers;
    std::vector<vtkPolyData *> polys;
    int current = 0;
    bool hasNoScalarsState = false;
};
