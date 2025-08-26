#include <vtkNew.h>
#include <vtkGenericDataObjectReader.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkNamedColors.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkProperty.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vector>
#include <string>
#include <iostream>
#include <vtkScalarBarActor.h>
#include <vtkLookupTable.h>
#include <vtkTextProperty.h>

#define BACKGROUND_COLOR "201F1F"
#define DEFAULT_MESH_COLOR "White"
#define COLORBAR_TITLE "Scalars"
#define COLORBAR_WIDTH 100
#define COLORBAR_HEIGHT 400

class ScalarCyclerStyle : public vtkInteractorStyleTrackballCamera
{
public:
    static ScalarCyclerStyle *New();
    vtkTypeMacro(ScalarCyclerStyle, vtkInteractorStyleTrackballCamera);

    void SetScalars(const std::vector<std::string> &names, vtkPolyDataMapper *mapper, vtkPolyData *poly, vtkRenderWindow *win, vtkScalarBarActor *bar)
    {
        this->ScalarNames = names;
        this->Mapper = mapper;
        this->Poly = poly;
        this->Win = win;
        this->Bar = bar;
        this->HasNoScalarsState = true;
    }

    void UpdateColorbar(const std::string &name, bool show = true)
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
        auto arr = Poly->GetPointData()->GetArray(name.c_str());
        if (!arr)
            return;
        double range[2];
        arr->GetRange(range);

        vtkLookupTable *lut = vtkLookupTable::SafeDownCast(Mapper->GetLookupTable());
        if (!lut)
        {
            vtkNew<vtkLookupTable> newlut;
            newlut->SetNumberOfTableValues(256);
            newlut->SetRange(range);
            newlut->SetHueRange(0.0, 0.8); // Red to purple
            newlut->Build();
            Mapper->SetLookupTable(newlut);
            lut = newlut;
        }
        else
        {
            lut->SetRange(range);
            lut->SetHueRange(0.0, 0.8); // Red to purple
            lut->Build();
        }
        Mapper->SetLookupTable(lut);
        Mapper->SetScalarRange(range);
        Bar->SetLookupTable(lut);
        Bar->SetTitle(name.c_str());
        Bar->SetNumberOfLabels(5);
        Bar->SetUnconstrainedFontSize(true);
        int *size = Win->GetSize();
        int barWidth = std::max(80, size[0] / 10);  // 10% of window width, min 80px
        int barHeight = std::max(200, size[1] / 2); // 50% of window height, min 200px
        Bar->SetMaximumWidthInPixels(barWidth);
        Bar->SetMaximumHeightInPixels(barHeight);
        int fontSize = std::max(10, barHeight / 15);
        Bar->GetLabelTextProperty()->SetFontSize(fontSize);
        Bar->GetTitleTextProperty()->SetFontSize(fontSize + 2);
    }

    void OnKeyPress() override
    {
        /*
         * Callback when the space key is pressed.
         * It cycles through the scalars and updates the colorbar.
         * Includes a "No Scalars" state.
         */
        std::string key = this->GetInteractor()->GetKeySym();
        int n = ScalarNames.size();
        if (key == "space" && n > 0)
        {
            Current = (Current + 1) % (n + (HasNoScalarsState ? 1 : 0));
            if (HasNoScalarsState && Current == n)
            {
                Mapper->ScalarVisibilityOff();
                Bar->SetVisibility(false);
                Poly->GetPointData()->SetActiveScalars(nullptr);
                std::string title = "VTK Viewer - No Scalars";
                Win->SetWindowName(title.c_str());
                UpdateColorbar("", false);
                Win->Render();
            }
            else
            {
                const std::string &name = ScalarNames[Current];
                Poly->GetPointData()->SetActiveScalars(name.c_str());
                Mapper->SelectColorArray(name.c_str());
                Mapper->SetScalarModeToUsePointData();
                Mapper->SetColorModeToMapScalars();
                Mapper->ScalarVisibilityOn();
                Bar->SetVisibility(true);
                UpdateColorbar(name, true);
                std::string title = "VTK Viewer - " + name;
                Win->SetWindowName(title.c_str());
                Win->Render();
            }
        }
        this->Superclass::OnKeyPress();
    }

    void OnConfigure() override
    {
        if (!ScalarNames.empty())
        {
            const std::string &name = ScalarNames[Current];
            UpdateColorbar(name);
            Win->Render();
        }
        this->Superclass::OnConfigure();
    }

private:
    std::vector<std::string> ScalarNames;
    vtkPolyDataMapper *Mapper = nullptr;
    vtkPolyData *Poly = nullptr;
    vtkRenderWindow *Win = nullptr;
    vtkScalarBarActor *Bar = nullptr;
    int Current = 0;
    bool HasNoScalarsState = false;
};
vtkStandardNewMacro(ScalarCyclerStyle);

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " file.vtk" << std::endl;
        return 1;
    }
    vtkNew<vtkGenericDataObjectReader> reader;
    reader->SetFileName(argv[1]);
    reader->Update();

    vtkPolyData *poly = vtkPolyData::SafeDownCast(reader->GetOutput());
    if (!poly)
    {
        std::cerr << "Not a polydata file!" << std::endl;
        return 2;
    }

    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputData(poly);

    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);

    vtkNew<vtkNamedColors> colors;
    actor->GetProperty()->SetColor(colors->GetColor3d(DEFAULT_MESH_COLOR).GetData());

    vtkNew<vtkRenderer> renderer;
    renderer->AddActor(actor);
    renderer->SetBackground(colors->GetColor3d(BACKGROUND_COLOR).GetData());

    // Add colorbar
    vtkNew<vtkScalarBarActor> bar;
    bar->SetTitle(COLORBAR_TITLE);
    bar->SetNumberOfLabels(5);
    renderer->AddActor2D(bar);

    vtkNew<vtkRenderWindow> window;
    window->AddRenderer(renderer);

    int winWidth = 1200;
    int winHeight = 900;

    window->SetSize(winWidth, winHeight);
    window->SetPosition(100, 100);
    window->SetWindowName("VTK Viewer");

    vtkNew<vtkRenderWindowInteractor> interactor;
    interactor->SetRenderWindow(window);

    // Gather all point scalar array names
    std::vector<std::string> scalars;
    vtkPointData *pd = poly->GetPointData();
    for (int i = 0; i < pd->GetNumberOfArrays(); ++i)
    {
        if (pd->GetArray(i))
        {
            scalars.push_back(pd->GetArray(i)->GetName());
        }
    }

    vtkNew<ScalarCyclerStyle> style;
    style->SetScalars(scalars, mapper, poly, window, bar);
    interactor->SetInteractorStyle(style);

    // Initialize with first scalar if no scalars are active by default
    if (!scalars.empty())
    {
        const char *active = poly->GetPointData()->GetScalars() ? poly->GetPointData()->GetScalars()->GetName() : nullptr;
        if (!active)
        {

            poly->GetPointData()->SetActiveScalars(scalars[0].c_str());
            mapper->SelectColorArray(scalars[0].c_str());
            mapper->SetScalarModeToUsePointData();
            mapper->SetColorModeToMapScalars();
            mapper->ScalarVisibilityOn();
        }
        style->UpdateColorbar(scalars[0], true);
    }

    window->Render();
    interactor->Start();
    return 0;
}