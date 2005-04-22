#include <string.h>
#include "Annotate.h"
#include "annotate.h"

/*
 * Class:     Annotate
 * Method:    init
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_Annotate_init(JNIEnv *env, jclass cls) {
	ANNOTATE_INIT();
}

/*
 * Class:     Annotate
 * Method:    startTask
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_Annotate_startTask(JNIEnv *env, jclass cls, jstring _roles, jint level, jstring _name) {
	const char *name = (*env)->GetStringUTFChars(env, _name, 0);
	const char *roles = _roles ? (*env)->GetStringUTFChars(env, _roles, 0) : NULL;
	ANNOTATE_START_TASK(roles, level, name);
	(*env)->ReleaseStringUTFChars(env, _name, name);
	if (roles) (*env)->ReleaseStringUTFChars(env, _roles, roles);
}

/*
 * Class:     Annotate
 * Method:    endTask
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_Annotate_endTask(JNIEnv *env, jclass cls, jstring _roles, jint level, jstring _name) {
	const char *name = (*env)->GetStringUTFChars(env, _name, 0);
	const char *roles = _roles ? (*env)->GetStringUTFChars(env, _roles, 0) : NULL;
	ANNOTATE_END_TASK(roles, level, name);
	(*env)->ReleaseStringUTFChars(env, _name, name);
	if (roles) (*env)->ReleaseStringUTFChars(env, _roles, roles);
}

/*
 * Class:     Annotate
 * Method:    setPathID
 * Signature: ([B)V
 */
JNIEXPORT void JNICALL Java_Annotate_setPathID(JNIEnv *env, jclass cls,
		jstring _roles, jint level, jbyteArray arr) {
	jsize len = (*env)->GetArrayLength(env, arr);
	jbyte *body = (*env)->GetByteArrayElements(env, arr, 0);
	const char *roles = _roles ? (*env)->GetStringUTFChars(env, _roles, 0) : NULL;
	ANNOTATE_SET_PATH_ID(roles, level, body, len);
	(*env)->ReleaseByteArrayElements(env, arr, body, 0);
	if (roles) (*env)->ReleaseStringUTFChars(env, _roles, roles);
}

/*
 * Class:     Annotate
 * Method:    getPathID
 * Signature: ()[B
 */
JNIEXPORT jbyteArray JNICALL Java_Annotate_getPathID(JNIEnv *env, jclass cls) {
	int len;
	const void *pathid = ANNOTATE_GET_PATH_ID(&len);
	jbyteArray arr = (*env)->NewByteArray(env, len);
	jbyte *body = (*env)->GetByteArrayElements(env, arr, 0);
	memcpy(body, pathid, len);
	(*env)->ReleaseByteArrayElements(env, arr, body, 0);
	return arr;
}

/*
 * Class:     Annotate
 * Method:    endPathID
 * Signature: ([B)V
 */
JNIEXPORT void JNICALL Java_Annotate_endPathID(JNIEnv *env, jclass cls,
		jstring _roles, jint level, jbyteArray arr) {
	jsize len = (*env)->GetArrayLength(env, arr);
	jbyte *body = (*env)->GetByteArrayElements(env, arr, 0);
	const char *roles = _roles ? (*env)->GetStringUTFChars(env, _roles, 0) : NULL;
	ANNOTATE_END_PATH_ID(roles, level, body, len);
	(*env)->ReleaseByteArrayElements(env, arr, body, 0);
	if (roles) (*env)->ReleaseStringUTFChars(env, _roles, roles);
}

/*
 * Class:     Annotate
 * Method:    event
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_Annotate_notice(JNIEnv *env, jclass cls, jstring _roles, jint level, jstring _str) {
	const char *str = (*env)->GetStringUTFChars(env, _str, 0);
	const char *roles = _roles ? (*env)->GetStringUTFChars(env, _roles, 0) : NULL;
	ANNOTATE_NOTICE(roles, level, str);
	(*env)->ReleaseStringUTFChars(env, _str, str);
	if (roles) (*env)->ReleaseStringUTFChars(env, _roles, roles);
}

/*
 * Class:     Annotate
 * Method:    send
 * Signature: ([BI)V
 */
JNIEXPORT void JNICALL Java_Annotate_send(JNIEnv *env, jclass cls,
		jstring _roles, jint level, jbyteArray arr, jint size) {
	jsize len = (*env)->GetArrayLength(env, arr);
	jbyte *body = (*env)->GetByteArrayElements(env, arr, 0);
	const char *roles = _roles ? (*env)->GetStringUTFChars(env, _roles, 0) : NULL;
	ANNOTATE_SEND(roles, level, body, len, size);
	(*env)->ReleaseByteArrayElements(env, arr, body, 0);
	if (roles) (*env)->ReleaseStringUTFChars(env, _roles, roles);
}

/*
 * Class:     Annotate
 * Method:    receive
 * Signature: ([BI)V
 */
JNIEXPORT void JNICALL Java_Annotate_receive(JNIEnv *env, jclass cls,
		jstring _roles, jint level, jbyteArray arr, jint size) {
	jsize len = (*env)->GetArrayLength(env, arr);
	jbyte *body = (*env)->GetByteArrayElements(env, arr, 0);
	const char *roles = _roles ? (*env)->GetStringUTFChars(env, _roles, 0) : NULL;
	ANNOTATE_RECEIVE(roles, level, body, len, size);
	(*env)->ReleaseByteArrayElements(env, arr, body, 0);
	if (roles) (*env)->ReleaseStringUTFChars(env, _roles, roles);
}
