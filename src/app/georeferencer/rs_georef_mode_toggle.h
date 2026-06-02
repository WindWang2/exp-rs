#pragma once

#include <QWidget>

class QButtonGroup;

/**
 * \brief Segmented button widget that selects the georeferencing mode.
 *
 * Three exclusive modes: Image → Map (default), Image → Image, RPC 物理模型.
 * Emits modeChanged() when the user clicks a button or when setMode() switches.
 */
class RsGeorefModeToggle : public QWidget
{
    Q_OBJECT

  public:
    enum Mode
    {
      ImageToMap = 0,
      ImageToImage = 1,
      RpcPhysical = 2
    };
    Q_ENUM( Mode )

    explicit RsGeorefModeToggle( QWidget *parent = nullptr );

    Mode currentMode() const { return mMode; }
    void setMode( Mode m );

  signals:
    void modeChanged( Mode newMode );

  private:
    Mode mMode = ImageToMap;
    QButtonGroup *mGroup = nullptr;
};
