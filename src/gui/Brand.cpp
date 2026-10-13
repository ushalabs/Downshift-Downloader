#include "Brand.h"
#include <QResource>
#include <QImage>
#include <QPixmap>
static void initializeBranding() { Q_INIT_RESOURCE(branding); }
namespace downshift {
QIcon brandIcon() {
    static const bool initialized=[] {initializeBranding();return true;}();
    (void)initialized;
    // The pinned Qt build omits PNG and its optional ICO reader plugin.
    // Use embedded RGBA directly, independent of installed image plugins.
    const QResource pixels(":/branding/downshift.rgba");
    const auto bytes=pixels.uncompressedData();
    if(bytes.size()!=256*256*4){qWarning("Brand resource: valid=%d stored=%lld decoded=%lld",pixels.isValid(),static_cast<long long>(pixels.size()),static_cast<long long>(bytes.size()));return {};}
    const QImage image(reinterpret_cast<const uchar*>(bytes.constData()),256,256,256*4,QImage::Format_RGBA8888);
    return QIcon(QPixmap::fromImage(image.copy()));
}
}
