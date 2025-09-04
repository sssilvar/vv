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

class ScalarCyclerStyle : public vtkInteractorStyleTrackballCamera
{
public:
    static ScalarCyclerStyle *New();
    vtkTypeMacro(ScalarCyclerStyle, vtkInteractorStyleTrackballCamera);
    void SetScalars(const std::vector<std::string> &names,
                    const std::vector<vtkPolyDataMapper *> &mappers,
                    const std::vector<vtkPolyData *> &polys,
                    vtkRenderWindow *win,
                    vtkScalarBarActor *bar);
    void UpdateColorbar(const std::string &name, bool show = true);
    void UpdateColorbar(const std::string &name, bool show, vtkPolyData *poly, vtkPolyDataMapper *mapper);
    void OnKeyPress() override;
    void SetActors(const std::vector<vtkActor *> &actors, const std::vector<std::array<double, 3>> &baseColors);

private:
    std::vector<vtkActor *> Actors;
    std::vector<std::array<double, 3>> BaseColors;
    std::vector<std::string> ScalarNames;
    std::vector<vtkPolyDataMapper *> Mappers;
    std::vector<vtkPolyData *> Polys;
    vtkPolyDataMapper *Mapper = nullptr;
    vtkPolyData *Poly = nullptr;
    vtkRenderWindow *Win = nullptr;
    vtkScalarBarActor *Bar = nullptr;
    int Current = 0;
    bool HasNoScalarsState = false;
};
