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

extern const char* kVVWindowTitle;

void ScalarCyclerStyle::initialize(const RendererContext& ctx,
                                   const std::vector<std::string>& names,
                                   const std::vector<vtkPolyDataMapper *>& mappers,
                                   const std::vector<vtkPolyData *>& polys)
{
    scalarNames = names;
    this->mappers = mappers;
    this->polys = polys;
    window = ctx.window;
    bar = ctx.bar;
    hasNoScalarsState = true;
}

void ScalarCyclerStyle::UpdateColorbar(const std::string &name, bool show)
{
    if (!polys.empty() && !mappers.empty())
        UpdateColorbar(name, show, polys[0], mappers[0]);
}

void ScalarCyclerStyle::UpdateColorbar(const std::string &name, bool show, vtkPolyData *poly, vtkPolyDataMapper *mapper)
{
    if (!show)
    {
        bar->SetLookupTable(nullptr);
        bar->SetTitle("");
        bar->SetNumberOfLabels(0);
        bar->GetLabelTextProperty()->SetFontSize(1);
        bar->GetTitleTextProperty()->SetFontSize(1);
        return;
    }
    auto arr = poly->GetPointData()->GetArray(name.c_str());
    if (!arr) return;
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
    bar->SetLookupTable(lut);
    bar->SetTitle(name.c_str());
    bar->SetNumberOfLabels(5);
    bar->SetUnconstrainedFontSize(true);
    const int *size = window->GetSize();
    int barWidth = std::max(80, size[0] / 10);
    int barHeight = std::max(200, size[1] / 2);
    bar->SetMaximumWidthInPixels(barWidth);
    bar->SetMaximumHeightInPixels(barHeight);
    int fontSize = std::max(10, barHeight / 15);
    bar->GetLabelTextProperty()->SetFontSize(fontSize);
    bar->GetTitleTextProperty()->SetFontSize(fontSize + 2);
}

void ScalarCyclerStyle::SetActors(const std::vector<vtkActor *> &actors, const std::vector<std::array<double, 3>> &baseColors)
{
    this->actors.clear();
    for (auto *a : actors) this->actors.push_back(a);
    this->baseColors = baseColors;
}

void ScalarCyclerStyle::OnKeyPress()
{
    std::string key = this->GetInteractor()->GetKeySym();
    int n = static_cast<int>(scalarNames.size());
    if (key == "space" && n > 0)
    {
        current = (current + 1) % (n + (hasNoScalarsState ? 1 : 0));
        if (hasNoScalarsState && current == n)
        {
            for (auto *mapper : mappers)
                mapper->ScalarVisibilityOff();
            bar->SetVisibility(false);
            for (auto *poly : polys)
                poly->GetPointData()->SetActiveScalars(nullptr);
            for (size_t i = 0; i < actors.size(); ++i)
            {
                std::array<double, 3> color;
                if (i < baseColors.size())
                    color = baseColors[i];
                else
                    color = generateDistinctColor(static_cast<int>(i));
                if (actors[i])
                    actors[i]->GetProperty()->SetColor(color[0], color[1], color[2]);
            }
            window->SetWindowName((std::string(kVVWindowTitle) + " - No Scalars").c_str());
            UpdateColorbar("", false);
            window->Render();
        }
        else
        {
            const std::string &name = scalarNames[current];
            bool found = false;
            for (size_t i = 0; i < polys.size(); ++i)
            {
                auto *arr = polys[i]->GetPointData()->GetArray(name.c_str());
                if (arr)
                {
                    polys[i]->GetPointData()->SetActiveScalars(name.c_str());
                    mappers[i]->SelectColorArray(name.c_str());
                    mappers[i]->SetScalarModeToUsePointData();
                    mappers[i]->SetColorModeToMapScalars();
                    mappers[i]->ScalarVisibilityOn();
                    if (i < baseColors.size() && actors[i])
                        actors[i]->GetProperty()->SetColor(baseColors[i][0], baseColors[i][1], baseColors[i][2]);
                    if (!found)
                    {
                        bar->SetVisibility(true);
                        UpdateColorbar(name, true, polys[i], mappers[i]);
                        found = true;
                    }
                }
                else
                {
                    mappers[i]->ScalarVisibilityOff();
                }
            }
            std::string title = std::string(kVVWindowTitle) + " - " + name;
            window->SetWindowName(title.c_str());
            window->Render();
        }
    }
    this->Superclass::OnKeyPress();
}

vtkStandardNewMacro(ScalarCyclerStyle);
