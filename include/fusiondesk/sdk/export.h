#ifndef FUSIONDESK_SDK_EXPORT_H
#define FUSIONDESK_SDK_EXPORT_H

#if defined(_WIN32) && defined(FUSIONDESK_BUILD_SHARED)
#if defined(FUSIONDESK_BUILDING_LIBRARY)
#define FUSIONDESK_API __declspec(dllexport)
#else
#define FUSIONDESK_API __declspec(dllimport)
#endif
#else
#define FUSIONDESK_API
#endif

#endif // FUSIONDESK_SDK_EXPORT_H
