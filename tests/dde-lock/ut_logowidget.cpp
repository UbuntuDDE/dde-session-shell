#include "logowidget.h"

#include <gtest/gtest.h>

class UT_LogWidget : public testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;

    LogoWidget *logoWidget;
};

void UT_LogWidget::SetUp()
{
    logoWidget = new LogoWidget();
}

void UT_LogWidget::TearDown()
{
    delete logoWidget;
}

TEST_F(UT_LogWidget, init)
{
    logoWidget->updateLocale("en_US.UTF-8");
}
