/*
 * twits.c

 * Copyright (c) 2010, Jeremiah LaRocco jeremiah.larocco@gmail.com

 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.

 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

 */

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <stdbool.h>

#define __USE_XOPEN  // For strptime
#include <time.h>

extern long timezone;

#include <curl/curl.h>

#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

struct download {
    size_t size;
    void *data;
};

struct twit_post {
    time_t postDate;
    char* msg;
};

// Allocate memory for, and parse fields for a twit_post
void fillPost(struct twit_post *np, char *str, char *postDate) {
    struct tm pd = {0};

    /* Thu May 06 06:20:06 +0000 2010 */
    if (strptime(postDate, "%a %b %d %H:%M:%S +0000 %Y", &pd) ==  NULL) {
        // Fail terribly
        np->postDate = 0;
    } else {
        np->postDate = mktime(&pd) - timezone;
    }

    size_t sl = strlen(str);
    np->msg = malloc(sl+1);
    strncpy(np->msg, str, sl+1);
}

// Delete memory allocated for a twit_post
void freePost(struct twit_post *np) {
    free(np->msg);
}

// Sort by date
int comparePostDate(const void *a, const void *b) {
    const struct twit_post *p1 = a;
    const struct twit_post *p2 = b;
    if (p1->postDate < p2->postDate) return -1;
    if (p1->postDate > p2->postDate) return 1;
    return 0;
}

// Called when libcurl receives more data
size_t grab_data(void *buffer, size_t size, size_t nmemb, void *userp) {

    // userp is a pointer to a download structure
    struct download *d = userp;

    // The size of the new data
    size_t thisSize = size * nmemb;

    // If there's nothing, just return.  Should never happen, but check anyway
    if (thisSize == 0) return 0;

    // Allocate new space
    size_t newSize = d->size + thisSize;
    void *nm = realloc(d->data, newSize+1);
    if (NULL == nm) {
        printf("Realloc failed, bitch!");
        return 0;
    }

    // Copy everything over
    memcpy(nm + d->size, buffer, thisSize);
    ((char*)nm)[newSize] = '\0';

    // Overwrite the old values with the new
    d->data = nm;
    d->size = newSize;

    // returning anything but (nmemb*size) signals an error to libcurl
    return thisSize;
}

// Download the given url into d
bool download(CURL *curl, char *url, struct download *d) {
    CURLcode res;

    if (curl == NULL) return false;
    // Set the URL
    curl_easy_setopt(curl, CURLOPT_URL, url);

    // Set the userp parameter used for grab_data
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, d);

    // Tell libcurl to call grab_data when it has new data
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, grab_data);

    // Do the download
    res = curl_easy_perform(curl);
    if (res != 0) {
        printf("An error occured while downloading \"%s\"!\n", url);
        // Hard to say whether this should be freed here or not...
        free(d->data);
        d->data = NULL;
        return false;
    }
    return true;
}

void initializeLibs(CURL **curl) {
    // Initialize cUrl
    curl_global_init(CURL_GLOBAL_NOTHING);
    *curl = curl_easy_init();

    // Initialize libxml
    xmlInitParser();
}

void cleanupLibs(CURL *curl) {
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    xmlCleanupParser();
}

void parseArgs(int argc, char *argv[], char account[64], int *num) {
    
    // Handle command line
    if (argc > 1) {
        strncpy(account, argv[1], sizeof(account));
    }
    if (argc > 2) {
        char *end;
        *num = strtol(argv[2], &end, 10);
        if (end == argv[2]) {
            *num = 10;
        }
        if (*num > 200) {
            printf("Can only fetch up to 200 tweets at a time.\n");
            *num = 200;
        }
    }
    if (argc >3) {
        printf("Ignoring extra command line arguments!");
    }
}

xmlXPathContextPtr xmlXPathNewContextNode(xmlDocPtr doc, xmlNodePtr node) {
    xmlXPathContextPtr retval = xmlXPathNewContext(doc);
    retval->node = node;
    return retval;
}
char *getText(char *buffer, size_t sz, char *xpath, xmlNodePtr node) {
    char *rv = NULL;

    char xpBuff[1024];
    snprintf(xpBuff, sizeof(xpBuff), "%s/text()", xpath);

    xmlXPathContextPtr context = xmlXPathNewContextNode(node->doc, node);
    xmlXPathObjectPtr txt = xmlXPathEvalExpression((xmlChar*)xpBuff, context);

    if (txt == NULL || xmlXPathNodeSetIsEmpty(txt->nodesetval)) {
        rv = NULL;
    } else {
        xmlChar *tVal = txt->nodesetval->nodeTab[0]->content;
        strncpy(buffer, (char*)tVal, sz);
        rv = buffer;
    }

    if (txt != NULL) xmlXPathFreeObject(txt);
    xmlXPathFreeContext(context);
    return rv;
}

int main(int argc, char *argv[]) {

    char account[64] = "jl_2";
    int numTwits = 10;

    CURL *curl = NULL;
    initializeLibs(&curl);

    parseArgs(argc, argv, account, &numTwits);

    char urlBuff[512];
    snprintf(urlBuff, sizeof(urlBuff), "http://twitter.com/statuses/user_timeline/%s.xml?count=%d", account, numTwits);
    
    struct download data = {0};

    if (!download(curl, urlBuff, &data)) {
        cleanupLibs(curl);
        exit(1);
    }

    // Parse the twitter xml
    xmlDocPtr doc = xmlParseDoc((xmlChar*)data.data);

    // Create initial xpath context
    xmlXPathContextPtr context = xmlXPathNewContext(doc);

    // Look for status entries with text and created_at subnodes
    xmlXPathObjectPtr stats = xmlXPathEvalExpression((xmlChar*)"/statuses/status[text][created_at]", context);
    if (stats == NULL || xmlXPathNodeSetIsEmpty(stats->nodesetval)) {
        printf("No tweets found.\n");

    } else {
        char dateBuffer[64];
        char txtBuffer[256];

        // Find out how many posts there were and allocate memory for them
        int numPosts = stats->nodesetval->nodeNr;
        struct twit_post *posts = malloc(sizeof(struct twit_post) * numPosts);

        // Loop through the posts and fill in the posts array
        for(int i = 0; i<numPosts; ++i) {
            fillPost(&(posts[i]),
                     getText(txtBuffer, sizeof(txtBuffer), "text", stats->nodesetval->nodeTab[i]),
                     getText(dateBuffer, sizeof(dateBuffer), "created_at", stats->nodesetval->nodeTab[i]));
        }

        // Sort by date
        qsort(posts, numPosts, sizeof(struct twit_post), comparePostDate);

        // Loop through the sorted post array and print the tweets
        for (int j=0; j<numPosts; ++j) {

            struct tm *tmp = localtime(&(posts[j].postDate));
            strftime(dateBuffer, sizeof(dateBuffer), "%m/%d/%Y %I:%M %p", tmp);

            printf("%s - %s\n", dateBuffer, posts[j].msg);

            // entries in posts aren't used again, so free them now
            // saves us from looping through again and freeing them later
            freePost(&posts[j]);
        }

        free(posts);
    }

    // Cleanup
    if (stats) xmlXPathFreeObject(stats);
    xmlXPathFreeContext(context);
    xmlFreeDoc(doc);

    free(data.data);

    cleanupLibs(curl);

    return 0;
}
