#ifndef TAPUI_NFC_TAG_H
#define TAPUI_NFC_TAG_H

/* Starts the NFCT peripheral as a read-only Type 2 tag holding uri. */
int nfc_tag_start(const char *uri);

#endif
