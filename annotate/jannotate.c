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
JNIEXPORT void JNICALL Java_Annotate_startTask(JNIEnv *env, jclass cls, jstring _name) {
	const char *name = (*env)->GetStringUTFChars(env, _name, 0);
	ANNOTATE_START_TASK(name);
	(*env)->ReleaseStringUTFChars(env, _name, name);
}

/*
 * Class:     Annotate
 * Method:    endTask
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_Annotate_endTask(JNIEnv *env, jclass cls, jstring _name) {
	const char *name = (*env)->GetStringUTFChars(env, _name, 0);
	ANNOTATE_END_TASK(name);
	(*env)->ReleaseStringUTFChars(env, _name, name);
}

/*
 * Class:     Annotate
 * Method:    setPathID
 * Signature: ([B)V
 */
JNIEXPORT void JNICALL Java_Annotate_setPathID(JNIEnv *env, jclass cls,
		jbyteArray arr) {
	jsize len = (*env)->GetArrayLength(env, arr);
	jbyte *body = (*env)->GetByteArrayElements(env, arr, 0);
	ANNOTATE_SET_PATH_ID(body, len);
	(*env)->ReleaseByteArrayElements(env, arr, body, 0);
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
		jbyteArray arr) {
	jsize len = (*env)->GetArrayLength(env, arr);
	jbyte *body = (*env)->GetByteArrayElements(env, arr, 0);
	ANNOTATE_END_PATH_ID(body, len);
	(*env)->ReleaseByteArrayElements(env, arr, body, 0);
}

/*
 * Class:     Annotate
 * Method:    event
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_Annotate_notice(JNIEnv *env, jclass cls, jstring _str) {
	const char *str = (*env)->GetStringUTFChars(env, _str, 0);
	ANNOTATE_NOTICE(str);
	(*env)->ReleaseStringUTFChars(env, _str, str);
}

/*
 * Class:     Annotate
 * Method:    send
 * Signature: ([BI)V
 */
JNIEXPORT void JNICALL Java_Annotate_send(JNIEnv *env, jclass cls,
		jbyteArray arr, jint size) {
	jsize len = (*env)->GetArrayLength(env, arr);
	jbyte *body = (*env)->GetByteArrayElements(env, arr, 0);
	ANNOTATE_SEND(body, len, size);
	(*env)->ReleaseByteArrayElements(env, arr, body, 0);
}

/*
 * Class:     Annotate
 * Method:    receive
 * Signature: ([BI)V
 */
JNIEXPORT void JNICALL Java_Annotate_receive(JNIEnv *env, jclass cls,
		jbyteArray arr, jint size) {
	jsize len = (*env)->GetArrayLength(env, arr);
	jbyte *body = (*env)->GetByteArrayElements(env, arr, 0);
	ANNOTATE_RECEIVE(body, len, size);
	(*env)->ReleaseByteArrayElements(env, arr, body, 0);
}
