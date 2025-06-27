// ApiModule.cpp
//#include "pch.h" // if precompiled headers are enabled

#include <winsock2.h>
#include <ws2tcpip.h>
#include <regex> 
#include  <string>
extern "C" {
#include "httpd.h"
#include "http_config.h"
#include "http_protocol.h"
#include "http_request.h"
#include "ap_config.h"
#include "apr_strings.h"
#include "http_log.h"

}

// Request handler
static int api_handler(request_rec* r) {
    if (!r->handler || strcmp(r->handler, "api_handler") != 0) {
        return DECLINED;
    }

    ap_set_content_type(r, "application/json");

    if (strcmp(r->uri, "/api/hello") == 0 && r->method_number == M_GET) {
        ap_rputs(R"({"status":"success","message":"GET OK"})", r);
        return OK;
    }

    if (strcmp(r->uri, "/api/echo") == 0 && r->method_number == M_POST) {
        // Get the Content-Length header to know how much data we expect to read
        const char* lenStr = apr_table_get(r->headers_in, "Content-Length");
        int len = lenStr ? atoi(lenStr) : 0;  // Convert header string to integer length

        // Validate Content-Length: must be positive and not exceed 64KB
        if (len <= 0 || len > 64 * 1024) {
            ap_rputs(R"({"status":"error","message":"Invalid length"})", r);
            return HTTP_BAD_REQUEST;  // Return HTTP 400 if invalid length
        }

        // Allocate buffer from Apache's memory pool to hold incoming data + null terminator
        char* buf = (char*)apr_pcalloc(r->pool, len + 1);
        int read = 0, total = 0;
        char tmp[1024];  // Temporary buffer to read chunks of data

        // Prepare to read client data (handles chunked transfer encoding if any)
        if (ap_setup_client_block(r, REQUEST_CHUNKED_ERROR) != OK || !ap_should_client_block(r)) {
            ap_rputs(R"({"status":"error","message":"Read failed"})", r);
            return HTTP_BAD_REQUEST;  // Return error if setup fails or no data to read
        }

        // Read client data in chunks until all bytes are read
        while ((read = ap_get_client_block(r, tmp, sizeof(tmp))) > 0) {
            memcpy(buf + total, tmp, read);  // Copy chunk into main buffer
            total += read;                   // Keep track of total bytes read
        }

        buf[total] = '\0';  // Null-terminate the buffer to safely treat as string

        // Send back a JSON response echoing the received data
        ap_rprintf(r, R"({"status":"success","data":"%s"})", buf);
        return OK;  // Return HTTP 200 OK
    }

    // New POST file upload handler
    if (strcmp(r->uri, "/api/upload") == 0 && r->method_number == M_POST) {
        // 1. Check if Content-Type header exists and is multipart/form-data (required for file uploads)
        const char* contentType = apr_table_get(r->headers_in, "Content-Type");
        if (!contentType || strncmp(contentType, "multipart/form-data", 19) != 0) {
            ap_rputs(R"({"status":"error","message":"Content-Type must be multipart/form-data"})", r);
            return HTTP_BAD_REQUEST;  // Return 400 if not correct content type
        }

        // 2. Prepare to read the request body from client (handles chunked encoding if present)
        if (ap_setup_client_block(r, REQUEST_CHUNKED_ERROR) != OK || !ap_should_client_block(r)) {
            ap_rputs(R"({"status":"error","message":"Client block setup failed"})", r);
            return HTTP_BAD_REQUEST;  // Return 400 if unable to read client body
        }

        // 3. Open the file where uploaded data will be saved
        const char* filepath = "C:/Apache24/htdocs/uploaded_file.tmp";
        FILE* file = fopen(filepath, "wb");
        if (!file) {
            ap_rputs(R"({"status":"error","message":"Failed to open file for writing"})", r);
            return HTTP_INTERNAL_SERVER_ERROR;  // Return 500 if file can't be opened
        }

        // 4. Read data from client in chunks and write directly to the file
        char temp[8192];  // 8 KB temporary buffer
        int read = 0;
        while ((read = ap_get_client_block(r, temp, sizeof(temp))) > 0) {
            // Write the data chunk to the file
            if (fwrite(temp, 1, read, file) != (size_t)read) {
                fclose(file);  // Close file on error
                ap_rputs(R"({"status":"error","message":"File write error"})", r);
                return HTTP_INTERNAL_SERVER_ERROR;  // Return 500 if write fails
            }
        }

        // 5. Close the file after all data has been written
        fclose(file);

        // 6. Send JSON response indicating success and the path where the file is saved
        ap_rprintf(r, R"({"status":"success","message":"File uploaded","saved_to":"%s"})", filepath);

        return OK;  // Return 200 OK
    }


    if (strcmp(r->uri, "/api/fileupload") == 0 && r->method_number == M_POST) {
        // Step 0: Check Content-Type header for multipart/form-data
        const char* contentType = apr_table_get(r->headers_in, "Content-Type");
        if (!contentType || strncmp(contentType, "multipart/form-data", 19) != 0) {
            ap_rputs(R"({"status":"error","message":"Content-Type must be multipart/form-data"})", r);
            return HTTP_BAD_REQUEST;
        }

        // Step 1: Setup to read client request body
        if (ap_setup_client_block(r, REQUEST_CHUNKED_ERROR) != OK || !ap_should_client_block(r)) {
            ap_rputs(R"({"status":"error","message":"Client block setup failed"})", r);
            return HTTP_BAD_REQUEST;
        }

        // Step 2: Read full request body into buffer 
        const int MAX_UPLOAD = 1024 * 1024; 
        char* rawBuffer = (char*)apr_pcalloc(r->pool, MAX_UPLOAD);
        char temp[8192];
        int total = 0, read = 0;

        while ((read = ap_get_client_block(r, temp, sizeof(temp))) > 0) {
            if (total + read > MAX_UPLOAD) {
                ap_rputs(R"({"status":"error","message":"File too large"})", r);
                return HTTP_REQUEST_ENTITY_TOO_LARGE;
            }
            memcpy(rawBuffer + total, temp, read);
            total += read;
        }

        // Step 3: Convert raw buffer to std::string for easy regex parsing
        std::string body(rawBuffer, total);

        // Step 4: Extract filename from multipart form headers
        std::string filename = "uploaded_file.tmp"; // fallback filename
        std::regex filenameRegex("filename=\"([^\"]+)\"");
        std::smatch match;
        if (std::regex_search(body, match, filenameRegex)) {
            filename = match[1].str();
        }

        // Sanitize filename: strip any directory path (for security)
        size_t pos = filename.find_last_of("/\\");
        if (pos != std::string::npos) {
            filename = filename.substr(pos + 1);
        }

        // Step 5: Prepare the full path for saving the file
        std::string fullpath = "C:/Apache24/htdocs/" + filename;
        FILE* file = fopen(fullpath.c_str(), "wb");
        if (!file) {
            ap_rputs(R"({"status":"error","message":"Failed to open file for writing"})", r);
            return HTTP_INTERNAL_SERVER_ERROR;
        }

        // Step 6: Find file data boundaries inside the multipart body
        // Multipart format: headers end with \r\n\r\n, file data starts after that
        size_t fileStart = body.find("\r\n\r\n");
        if (fileStart == std::string::npos) {
            fclose(file);
            ap_rputs(R"({"status":"error","message":"Malformed multipart body"})", r);
            return HTTP_BAD_REQUEST;
        }
        fileStart += 4; // Skip past the header delimiter

        // Find the boundary that ends the file content, which starts with \r\n--
        size_t fileEnd = body.find("\r\n--", fileStart);
        if (fileEnd == std::string::npos) {
            fileEnd = body.size(); // Fallback to end of body
        }

        // Step 7: Write only the file content to disk
        fwrite(body.data() + fileStart, 1, fileEnd - fileStart, file);
        fclose(file);

        // Step 8: Send JSON response with success and saved file path
        ap_rprintf(r, R"({"status":"success","message":"File uploaded","saved_to":"%s"})", fullpath.c_str());
        return OK;
    }

    if (strcmp(r->uri, "/api/largefileupload") == 0 && r->method_number == M_POST) {
        // Step 1: Check Content-Type
        const char* contentType = apr_table_get(r->headers_in, "Content-Type");
        if (!contentType || strncmp(contentType, "multipart/form-data", 19) != 0) {
            ap_rputs(R"({"status":"error","message":"Content-Type must be multipart/form-data"})", r);
            return HTTP_BAD_REQUEST;
        }

        // Step 2: Setup client block reading
        if (ap_setup_client_block(r, REQUEST_CHUNKED_ERROR) != OK || !ap_should_client_block(r)) {
            ap_rputs(R"({"status":"error","message":"Client block setup failed"})", r);
            return HTTP_BAD_REQUEST;
        }

        // Step 3: Read initial chunk to extract headers
        char headChunk[8192];
        int read = ap_get_client_block(r, headChunk, sizeof(headChunk));
        if (read <= 0) {
            ap_rputs(R"({"status":"error","message":"Failed to read initial data"})", r);
            return HTTP_BAD_REQUEST;
        }

        std::string head(headChunk, read);

        // Step 4: Extract filename manually (no regex)
        std::string filename = "uploaded_file.tmp"; // fallback
        std::string marker = "filename=\"";
        size_t start = head.find(marker);
        if (start != std::string::npos) {
            start += marker.length();
            size_t end = head.find("\"", start);
            if (end != std::string::npos) {
                filename = head.substr(start, end - start);
            }
        }

        // Step 5: Sanitize filename (remove directory)
        size_t pos = filename.find_last_of("/\\");
        if (pos != std::string::npos) {
            filename = filename.substr(pos + 1);
        }

        // Step 6: Open output file for streaming
        std::string fullpath = "C:/Apache24/htdocs/" + filename;
        FILE* file = fopen(fullpath.c_str(), "wb");
        if (!file) {
            ap_rputs(R"({"status":"error","message":"Failed to open file for writing"})", r);
            return HTTP_INTERNAL_SERVER_ERROR;
        }

        // Step 7: Write from initial chunk after headers (\r\n\r\n)
        size_t contentStart = head.find("\r\n\r\n");
        if (contentStart != std::string::npos) {
            contentStart += 4;
            fwrite(head.data() + contentStart, 1, head.size() - contentStart, file);
        }

        // Step 8: Continue streaming rest of body to disk
        char buffer[8192];
        while ((read = ap_get_client_block(r, buffer, sizeof(buffer))) > 0) {
            fwrite(buffer, 1, read, file);
        }

        fclose(file);

        // Step 9: Send success response
        ap_rprintf(r, R"({"status":"success","message":"File uploaded","filename":"%s"})", filename.c_str());
        return OK;
    }




    return HTTP_NOT_FOUND;
}

static void register_hooks(apr_pool_t* p) {
    ap_hook_handler(api_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

extern "C" __declspec(dllexport) module api_module = {
    STANDARD20_MODULE_STUFF,
    NULL, NULL, NULL, NULL, NULL,
    register_hooks
};
