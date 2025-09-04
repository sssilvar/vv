#include "MeshRenderer.h"
#include "ScalarCyclerStyle.h"
#include <vtkNamedColors.h>
#include <vtkPointData.h>
#include <algorithm>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkScalarBarActor.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <set>

MeshRenderer::~MeshRenderer() = default;

MeshRenderer::MeshRenderer() {}

void MeshRenderer::setup(const std::vector<vtkSmartPointer<vtkPolyData>> &polys,
                         const std::vector<std::string> &names,
                         const std::vector<std::array<double, 3>> &colorsHex)
{
    renderer = vtkSmartPointer<vtkRenderer>::New();

    bar = vtkSmartPointer<vtkScalarBarActor>::New();
    bar->SetTitle("Scalars");
    bar->SetNumberOfLabels(5);
    renderer->AddActor2D(bar);
    bar->SetVisibility(false);

    mappers.clear();
    actors.clear();
    for (size_t i = 0; i < polys.size(); ++i)
    {
        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputData(polys[i]);
        mapper->ScalarVisibilityOff();
        vtkNew<vtkActor> actor;
        actor->SetMapper(mapper);
        actor->GetProperty()->SetColor(colorsHex[i][0], colorsHex[i][1], colorsHex[i][2]);
        actor->GetProperty()->SetOpacity(1.0);
        renderer->AddActor(actor);
        mappers.push_back(mapper);
        actors.push_back(actor);
    }

    window = vtkSmartPointer<vtkRenderWindow>::New();
    window->AddRenderer(renderer);
    int screenWidth = 1200, screenHeight = 1024;
    window->SetSize(screenWidth, screenHeight);
    window->SetPosition(100, 100);
    window->SetWindowName("VTK Viewer");

    interactor = vtkSmartPointer<vtkRenderWindowInteractor>::New();
    interactor->SetRenderWindow(window);

    // Collect all unique scalar names from all meshes
    std::set<std::string> scalarNameSet;
    for (const auto &poly : polys)
    {
        vtkPointData *pd = poly->GetPointData();
        for (int i = 0; i < pd->GetNumberOfArrays(); ++i)
            if (pd->GetArray(i) && pd->GetArray(i)->GetName())
                scalarNameSet.insert(pd->GetArray(i)->GetName());
    }
    std::vector<std::string> scalarNames(scalarNameSet.begin(), scalarNameSet.end());
    std::vector<vtkPolyDataMapper *> mapperPtrs;
    std::vector<vtkPolyData *> polyPtrs;
    for (size_t i = 0; i < mappers.size(); ++i)
    {
        mapperPtrs.push_back(mappers[i].GetPointer());
        polyPtrs.push_back(polys[i].GetPointer());
    }
    std::vector<vtkActor *> actorPtrs;
    for (size_t i = 0; i < actors.size(); ++i)
        actorPtrs.push_back(actors[i].GetPointer());
    vtkNew<ScalarCyclerStyle> style;
    if (!polys.empty() && !mappers.empty())
    {
        style->SetScalars(scalarNames, mapperPtrs, polyPtrs, window, bar);
        style->SetActors(actorPtrs, colorsHex);
    }
    interactor->SetInteractorStyle(style);

    // If the first block has scalars, initialize the colorbar with the first one
    if (!polys.empty() && !scalarNames.empty())
    {
        const std::string &firstScalar = scalarNames[0];
        bool found = false;
        for (size_t i = 0; i < polys.size(); ++i)
        {
            auto *arr = polys[i]->GetPointData()->GetArray(firstScalar.c_str());
            if (arr)
            {
                polys[i]->GetPointData()->SetActiveScalars(firstScalar.c_str());
                mappers[i]->SelectColorArray(firstScalar.c_str());
                mappers[i]->SetScalarModeToUsePointData();
                mappers[i]->SetColorModeToMapScalars();
                mappers[i]->ScalarVisibilityOn();
                if (!found)
                {
                    style->UpdateColorbar(firstScalar, true);
                    bar->SetVisibility(true);
                    found = true;
                }
            }
            else
            {
                mappers[i]->ScalarVisibilityOff();
            }
        }
    }
}

void MeshRenderer::start()
{
    window->Render();
    interactor->Start();
}
