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
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_Annotate_setPathID(JNIEnv *env, jclass cls, jint pathid) {
	ANNOTATE_SET_PATH_ID(pathid);
}

/*
 * Class:     Annotate
 * Method:    endPathID
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_Annotate_endPathID(JNIEnv *env, jclass cls, jint pathid) {
	ANNOTATE_END_PATH_ID(pathid);
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
 * Signature: (II)V
 */
JNIEXPORT void JNICALL Java_Annotate_send(JNIEnv *env, jclass cls, jint sender, jint msgid, jint size) {
	ANNOTATE_SEND(sender, msgid, size);
}

/*
 * Class:     Annotate
 * Method:    receive
 * Signature: (II)V
 */
JNIEXPORT void JNICALL Java_Annotate_receive(JNIEnv *env, jclass cls, jint sender, jint msgid, jint size) {
	ANNOTATE_RECEIVE(sender, msgid, size);
}
