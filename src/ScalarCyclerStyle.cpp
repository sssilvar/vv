#include "ScalarCyclerStyle.h"
#include "mesh_utils.h"
#include <vtkLookupTable.h>
#include <vtkNamedColors.h>
#include <vtkPointData.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkScalarBarActor.h>
#include <vtkTextProperty.h>
#include <algorithm>
#include <vtkRenderWindowInteractor.h>

void ScalarCyclerStyle::SetScalars(const std::vector<std::string> &names,
                                   const std::vector<vtkPolyDataMapper *> &mappers,
                                   const std::vector<vtkPolyData *> &polys,
                                   vtkRenderWindow *win,
                                   vtkScalarBarActor *bar)
{
    this->ScalarNames = names;
    this->Mappers = mappers;
    this->Polys = polys;
    this->Win = win;
    this->Bar = bar;
    this->HasNoScalarsState = true;
}

void ScalarCyclerStyle::UpdateColorbar(const std::string &name, bool show)
{
    // Use the first mesh and mapper if available
    if (!Polys.empty() && !Mappers.empty())
        UpdateColorbar(name, show, Polys[0], Mappers[0]);
}

void ScalarCyclerStyle::UpdateColorbar(const std::string &name, bool show, vtkPolyData *poly, vtkPolyDataMapper *mapper)
{
    if (!show)
    {
        Bar->SetLookupTable(nullptr);
        Bar->SetTitle("");
        Bar->SetNumberOfLabels(0);
        Bar->GetLabelTextProperty()->SetFontSize(1);
        Bar->GetTitleTextProperty()->SetFontSize(1);
        return;
    }
    auto arr = poly->GetPointData()->GetArray(name.c_str());
    if (!arr)
        return;
    double range[2];
    arr->GetRange(range);
    vtkLookupTable *lut = vtkLookupTable::SafeDownCast(mapper->GetLookupTable());
    if (!lut)
    {
        vtkNew<vtkLookupTable> newlut;
        newlut->SetNumberOfTableValues(256);
        newlut->SetRange(range);
        newlut->SetHueRange(0.0, 0.8);
        double meshColor[3];
        vtkNew<vtkNamedColors> colors;
        colors->GetColorRGB("Grey", meshColor);
        double nanColor[4] = {meshColor[0], meshColor[1], meshColor[2], 1.0};
        newlut->SetNanColor(nanColor);
        newlut->Build();
        mapper->SetLookupTable(newlut);
        lut = newlut;
    }
    else
    {
        lut->SetRange(range);
        lut->SetHueRange(0.0, 0.8);
        double meshColor[3];
        vtkNew<vtkNamedColors> colors;
        colors->GetColorRGB("Grey", meshColor);
        double nanColor[4] = {meshColor[0], meshColor[1], meshColor[2], 1.0};
        lut->SetNanColor(nanColor);
        lut->Build();
    }
    mapper->SetLookupTable(lut);
    mapper->SetScalarRange(range);
    Bar->SetLookupTable(lut);
    Bar->SetTitle(name.c_str());
    Bar->SetNumberOfLabels(5);
    Bar->SetUnconstrainedFontSize(true);
    const int *size = Win->GetSize();
    int barWidth = std::max(80, size[0] / 10);
    int barHeight = std::max(200, size[1] / 2);
    Bar->SetMaximumWidthInPixels(barWidth);
    Bar->SetMaximumHeightInPixels(barHeight);
    int fontSize = std::max(10, barHeight / 15);
    Bar->GetLabelTextProperty()->SetFontSize(fontSize);
    Bar->GetTitleTextProperty()->SetFontSize(fontSize + 2);
}

void ScalarCyclerStyle::SetActors(const std::vector<vtkActor *> &actors, const std::vector<std::array<double, 3>> &baseColors)
{
    this->Actors = actors;
    this->BaseColors = baseColors;
}

void ScalarCyclerStyle::OnKeyPress()
{
    std::string key = this->GetInteractor()->GetKeySym();
    int n = static_cast<int>(ScalarNames.size());
    if (key == "space" && n > 0)
    {
        Current = (Current + 1) % (n + (HasNoScalarsState ? 1 : 0));
        if (HasNoScalarsState && Current == n)
        {
            for (auto *mapper : Mappers)
                mapper->ScalarVisibilityOff();
            Bar->SetVisibility(false);
            for (auto *poly : Polys)
                poly->GetPointData()->SetActiveScalars(nullptr);
            // Set unique color for each actor
            for (size_t i = 0; i < Actors.size(); ++i)
            {
                std::array<double, 3> color;
                if (i < BaseColors.size())
                    color = BaseColors[i];
                else
                    color = generateDistinctColor(static_cast<int>(i));
                if (Actors[i])
                    Actors[i]->GetProperty()->SetColor(color[0], color[1], color[2]);
            }
            Win->SetWindowName("VTK Viewer - No Scalars");
            UpdateColorbar("", false);
            Win->Render();
        }
        else
        {
            const std::string &name = ScalarNames[Current];
            // Set scalar for all meshes that have it
            bool found = false;
            for (size_t i = 0; i < Polys.size(); ++i)
            {
                auto *arr = Polys[i]->GetPointData()->GetArray(name.c_str());
                if (arr)
                {
                    Polys[i]->GetPointData()->SetActiveScalars(name.c_str());
                    Mappers[i]->SelectColorArray(name.c_str());
                    Mappers[i]->SetScalarModeToUsePointData();
                    Mappers[i]->SetColorModeToMapScalars();
                    Mappers[i]->ScalarVisibilityOn();
                    if (i < BaseColors.size() && Actors[i])
                        Actors[i]->GetProperty()->SetColor(BaseColors[i][0], BaseColors[i][1], BaseColors[i][2]);
                    if (!found)
                    {
                        Bar->SetVisibility(true);
                        UpdateColorbar(name, true, Polys[i], Mappers[i]);
                        found = true;
                    }
                }
                else
                {
                    Mappers[i]->ScalarVisibilityOff();
                }
            }
            std::string title = "VTK Viewer - " + name;
            Win->SetWindowName(title.c_str());
            Win->Render();
        }
    }
    this->Superclass::OnKeyPress();
}

vtkStandardNewMacro(ScalarCyclerStyle);
