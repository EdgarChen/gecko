/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class org_mozilla_webclient_wrapper_0005fnative_NavigationImpl */

#ifndef _Included_org_mozilla_webclient_wrapper_0005fnative_NavigationImpl
#define _Included_org_mozilla_webclient_wrapper_0005fnative_NavigationImpl
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     org_mozilla_webclient_wrapper_0005fnative_NavigationImpl
 * Method:    nativeLoadURL
 * Signature: (ILjava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_mozilla_webclient_wrapper_1native_NavigationImpl_nativeLoadURL
  (JNIEnv *, jobject, jint, jstring);

/*
 * Class:     org_mozilla_webclient_wrapper_0005fnative_NavigationImpl
 * Method:    nativeRefresh
 * Signature: (IJ)V
 */
JNIEXPORT void JNICALL Java_org_mozilla_webclient_wrapper_1native_NavigationImpl_nativeRefresh
  (JNIEnv *, jobject, jint, jlong);

/*
 * Class:     org_mozilla_webclient_wrapper_0005fnative_NavigationImpl
 * Method:    nativeStop
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_org_mozilla_webclient_wrapper_1native_NavigationImpl_nativeStop
  (JNIEnv *, jobject, jint);

#ifdef __cplusplus
}
#endif
#endif
