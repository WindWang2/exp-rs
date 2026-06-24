// test_guided_workflow_widget.cpp — GuidedWorkflowWidget tests
#include <catch2/catch_test_macros.hpp>

#include <app/widgets/guided_workflow_widget.h>

TEST_CASE("WorkflowStep structure", "[widget][workflow]")
{
    SECTION("Default construction")
    {
        WorkflowStep step;
        REQUIRE(step.title.isEmpty());
        REQUIRE(step.description.isEmpty());
        REQUIRE(step.instructions.isEmpty());
        REQUIRE(step.actionId.isEmpty());
        REQUIRE(step.completionHint.isEmpty());
    }

    SECTION("Set fields")
    {
        WorkflowStep step;
        step.title = "Step 1";
        step.description = "Load data";
        step.instructions = "<p>Load a raster file</p>";
        step.actionId = "addRasterLayer";
        step.completionHint = "Layer appears in layer tree";

        REQUIRE(step.title == "Step 1");
        REQUIRE(step.description == "Load data");
        REQUIRE(step.actionId == "addRasterLayer");
    }
}

TEST_CASE("Workflow structure", "[widget][workflow]")
{
    SECTION("Default construction")
    {
        Workflow wf;
        REQUIRE(wf.id.isEmpty());
        REQUIRE(wf.title.isEmpty());
        REQUIRE(wf.description.isEmpty());
        REQUIRE(wf.steps.isEmpty());
    }

    SECTION("Workflow with steps")
    {
        Workflow wf;
        wf.id = "spectral_analysis";
        wf.title = "Spectral Analysis";
        wf.description = "Learn spectral indices";

        WorkflowStep step1;
        step1.title = "Load Image";
        step1.actionId = "addRasterLayer";

        WorkflowStep step2;
        step2.title = "Compute NDVI";
        step2.actionId = "openSpectralIndexDialog";

        wf.steps << step1 << step2;

        REQUIRE(wf.steps.size() == 2);
        REQUIRE(wf.steps[0].title == "Load Image");
        REQUIRE(wf.steps[1].title == "Compute NDVI");
    }
}

TEST_CASE("GuidedWorkflowWidget workflow data", "[widget][workflow]")
{
    SECTION("WorkflowStep fields are accessible")
    {
        WorkflowStep step;
        step.title = "Test Step";
        step.description = "Test Description";
        step.instructions = "<b>Bold</b> instructions";
        step.actionId = "testAction";
        step.completionHint = "Done when X";

        REQUIRE(step.title == "Test Step");
        REQUIRE(step.description == "Test Description");
        REQUIRE(step.instructions.contains("Bold"));
        REQUIRE(step.actionId == "testAction");
        REQUIRE(step.completionHint == "Done when X");
    }

    SECTION("Workflow can hold multiple steps")
    {
        Workflow wf;
        for (int i = 0; i < 10; i++) {
            WorkflowStep step;
            step.title = QString("Step %1").arg(i + 1);
            wf.steps.append(step);
        }
        REQUIRE(wf.steps.size() == 10);
        REQUIRE(wf.steps.last().title == "Step 10");
    }
}

TEST_CASE("Workflow signal types", "[widget][workflow]")
{
    SECTION("Workflow IDs can be compared")
    {
        Workflow wf1;
        wf1.id = "workflow_a";

        Workflow wf2;
        wf2.id = "workflow_b";

        REQUIRE(wf1.id != wf2.id);
    }

    SECTION("Empty workflow has no steps")
    {
        Workflow wf;
        REQUIRE(wf.steps.isEmpty());
    }
}
