#include "MeshRenderer.h"
#include "ScalarCyclerStyle.h"
#include <vtkNamedColors.h>
#include <vtkPointData.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkScalarBarActor.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <set>

const char* kVVWindowTitle = "VV mesh viewer";

MeshRenderer::~MeshRenderer() = default;
MeshRenderer::MeshRenderer() {}

void MeshRenderer::setup(const std::vector<vtkSmartPointer<vtkPolyData>> &polys,
                         const std::vector<std::string> &names,
                         const std::vector<std::array<double, 3>> &colorsHex)
{
    renderer = vtkSmartPointer<vtkRenderer>::New();

    context.window = vtkSmartPointer<vtkRenderWindow>::New();
    context.bar = vtkSmartPointer<vtkScalarBarActor>::New();
    context.bar->SetTitle("Scalars");
    context.bar->SetNumberOfLabels(5);
    renderer->AddActor2D(context.bar);
    context.bar->SetVisibility(false);

    mappers.clear();
    context.actors.clear();
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
        context.actors.push_back(actor);
    }
    context.colorsHex = colorsHex;

    context.window->AddRenderer(renderer);
    int screenWidth = 1200, screenHeight = 1024;
    context.window->SetSize(screenWidth, screenHeight);
    context.window->SetPosition(100, 100);
    context.window->SetWindowName(kVVWindowTitle);

    interactor = vtkSmartPointer<vtkRenderWindowInteractor>::New();
    interactor->SetRenderWindow(context.window);

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
    for (size_t i = 0; i < context.actors.size(); ++i)
        actorPtrs.push_back(context.actors[i].GetPointer());
    vtkNew<ScalarCyclerStyle> style;
    if (!polys.empty() && !mappers.empty())
    {
        style->initialize(context, scalarNames, mapperPtrs, polyPtrs);
        style->SetActors(actorPtrs, colorsHex);
    }
    interactor->SetInteractorStyle(style);

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
                    context.bar->SetVisibility(true);
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
    context.window->Render();
    interactor->Start();
}
