#ifndef ___HTTPSERVER_H___
#define ___HTTPSERVER_H___

#include <jnetlib/httpserv.h>

#define SEND_BUFFER		262144
#define RECIEVE_BUFFER	16384

#define Z_CHUNK SEND_BUFFER / 2
#define Z_CLEVEL Z_DEFAULT_COMPRESSION

#define FILE_BUFFER SEND_BUFFER / 2

#if (FILE_BUFFER_SIZE > Z_CHUNK)
#error file buffer size must be less or equal to Z_CHUNK
#endif

class foo_httpserver_command;

namespace httpc {
	namespace control {
		typedef pfc::map_t<const char *, bool (* )(foo_httpserver_command *), pfc::comparator_strcmp> commands_map;

		extern HANDLE process_command_event;

		extern void process_command(pfc::string_base &cmd, pfc::string_base &param1, pfc::string_base &param2, pfc::string_base &param3);
	}
}

class foo_httpserv : public JNL_HTTPServ
{
	enum send_codes {
		SEND_OK,
		SEND_INCOMPLETE,
		SEND_SOCKET_ERROR,
		SEND_IO_ERROR,
		SEND_ZLIB_ERROR
	};
private:
	unsigned char z_outbuf[Z_CHUNK]{};
	char filebuf[FILE_BUFFER]{};

	pfc::string8 m_err_str;
	size_t send_data(char *buf, size_t buf_len);
	size_t send_data_zlib(char *buf, size_t buf_len);
	size_t send_file(pfc::string_base &filepath);
	size_t send_file_zlib(pfc::string_base &filepath);
	size_t content_type(const char *request);
	bool is_gzip_encoding_supported();
	pfc::hires_timer timer;
	void foo_httpserv::log(pfc::string_base &cmd, pfc::string_base &param1, pfc::string_base &param2, pfc::string_base &param3, char *remotehost, pfc::string_base &request_url);
public:
	foo_httpserv(JNL_Connection *con) : JNL_HTTPServ(con) { }

	int threadnum{};

	void process_request();
};

class foo_httpserver_command
{
public:
	enum commands {
		E_PARAM1=0,
		E_PARAM2=1,
		E_PARAM3=2
	};

	foo_httpserver_command() { };
	foo_httpserver_command(pfc::string8 cmd, pfc::string8 param1, pfc::string8 param2, pfc::string8 param3) : m_cmd(cmd), m_param1(param1), m_param2(param2), m_param3(param3) { };

	pfc::string8 get_command() { return m_cmd; }
	pfc::string8 get_param(int index)
	{
	  switch(index) {
		case 0: return m_param1;
		case 1: return m_param2;
		case 2: return m_param3;
		default: return m_param1;
	  }
	}

private:
	pfc::string8 m_cmd;
	pfc::string8 m_param1;
	pfc::string8 m_param2;
	pfc::string8 m_param3;
};

class mimetypes {
private:
	pfc::map_t<const char *, const char *, pfc::comparator_stricmp_ascii> m_mimeTypes;
	pfc::avltree_t<const char*, pfc::comparator_stricmp_ascii> m_compressibleTypes;
public:

	mimetypes ()
	{
		m_mimeTypes["json"] = "application/json; charset=utf-8";
		m_mimeTypes["html"] = "text/html; charset=utf-8";
		m_mimeTypes["jpg"] = "image/jpeg";
		m_mimeTypes["js"] = "application/x-javascript; charset=utf-8";
		m_mimeTypes["css"] = "text/css";
		m_mimeTypes["ico"] = "image/x-icon";
		m_mimeTypes["gif"] = "image/gif";
		m_mimeTypes["jpe"] = m_mimeTypes["jpg"];
		m_mimeTypes["jpeg"] = m_mimeTypes["jpg"];
		m_mimeTypes["png"] = "image/png";
		m_mimeTypes["xml"] = "text/xml; charset=utf-8";
		m_mimeTypes["htm"] = m_mimeTypes["html"];

		m_compressibleTypes.add_item("json");
		m_compressibleTypes.add_item("html");
		m_compressibleTypes.add_item("js");
		m_compressibleTypes.add_item("css");
		m_compressibleTypes.add_item("xml");
		m_compressibleTypes.add_item("htm");
	}

	pfc::string get_content_type(pfc::string_base &request, bool &compressible)
	{
		pfc::string content_type;
		pfc::string extension = get_extension(request);

		compressible = false;

		if (!extension.is_empty())
		{
			if (m_mimeTypes.query(extension, content_type))
				compressible = m_compressibleTypes.contains(extension);
		}

		return content_type;
	}
};

extern mimetypes mime;

#endif //___HTTPSERVER_H___