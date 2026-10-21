# tools/cups_preview_server.py
"""
Local HTTP Preview Server for CUPS Web Interface & Root Intelligence.
Serves static assets from doc/ and dynamically renders CUPS templates with mock system state.
Supports both pt_BR and en locales.
"""
import os
import sys
import mimetypes
import urllib
import urllib.parse
from http.server import HTTPServer, BaseHTTPRequestHandler
BASE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if BASE_DIR not in sys.path:
    sys.path.insert(0, BASE_DIR)

from tools.cups_template import CupsTemplateEngine
PORT = 8631
DOC_DIR = os.path.join(BASE_DIR, "doc")
TEMPLATES_DIR = os.path.join(BASE_DIR, "templates")

# Mock data simulating a realistic CUPS daemon
MOCK_DATA = {
    "server": {
        "version": "CUPS 2.5b1 (Root Intelligence)",
        "server_name": "cups-srv01.local",
        "remote_user": "admin",
        "remote_name": "Targino (Admin)",
        "sid": "c9a4f2108b5e917d3324ba5c",
        "share_printers": "CHECKED",
        "max_clients": "100",
        "remote_admin": "CHECKED",
        "remote_any": "",
        "browse_web_if": "CHECKED",
        "user_cancel_any": "",
        "preserve_jobs": "CHECKED",
        "max_jobs": "500",
        "preserve_job_history": "30d",
        "preserve_job_files": "8h",
        "debug_logging": "",
        "max_log_size": "10m"
    },
    "printers": [
        {
            "printer_name": "LaserJet-Pro-M404dn",
            "printer_info": "HP LaserJet Pro M404dn (Departamento Técnico)",
            "printer_location": "Bancada 03 - Térreo",
            "printer_make_and_model": "HP LaserJet Pro M404dn, driverless, cups-filters 2.0",
            "printer_uri_supported": "/printers/LaserJet-Pro-M404dn",
            "printer_state": "3",  # 3=idle, 4=processing, 5=paused
            "printer_state_message": "",
            "printer_is_accepting_jobs": "1",
            "printer_is_shared": "1",
            "server_is_sharing_printers": "1",
            "default_name": "LaserJet-Pro-M404dn",
            "color_supported": "0",
            "sides_supported": "two-sided-long-edge",
            "device_uri": "socket://192.168.1.120:9100",
            "job_sheets_default": "none, none",
            "media_default": "iso_a4_210x297mm",
            "sides_default": "two-sided-long-edge"
        },
        {
            "printer_name": "Zebra-GK420t-Etiquetas",
            "printer_info": "Impressora Térmica de Etiquetas de Inventário",
            "printer_location": "Almoxarifado Geral",
            "printer_make_and_model": "Zebra ZPL Label Printer, CUPS+Gutenprint",
            "printer_uri_supported": "/printers/Zebra-GK420t-Etiquetas",
            "printer_state": "5",  # 5=paused / stopped
            "printer_state_message": "Fim de fita / Ribbon out - sensor detectou ausência de suprimento",
            "printer_is_accepting_jobs": "1",
            "printer_is_shared": "0",
            "server_is_sharing_printers": "1",
            "default_name": "LaserJet-Pro-M404dn",
            "color_supported": "0",
            "sides_supported": "one-sided",
            "device_uri": "usb://Zebra/GK420t?serial=18J143801552",
            "job_sheets_default": "none, none",
            "media_default": "custom_100x50mm",
            "sides_default": "one-sided"
        },
        {
            "printer_name": "WorkCentre-7835-Cor",
            "printer_info": "Xerox WorkCentre 7835 Multifuncional Corporativa",
            "printer_location": "Corredor Central - 2º Andar",
            "printer_make_and_model": "Xerox WorkCentre 7835 Postscript (Color)",
            "printer_uri_supported": "/printers/WorkCentre-7835-Cor",
            "printer_state": "4",  # 4=processing
            "printer_state_message": "Processando página 4 de 12 (rasterizando via pdftops)",
            "printer_is_accepting_jobs": "1",
            "printer_is_shared": "1",
            "server_is_sharing_printers": "1",
            "default_name": "LaserJet-Pro-M404dn",
            "color_supported": "1",
            "sides_supported": "two-sided-long-edge",
            "device_uri": "ipp://192.168.1.150/ipp/print",
            "job_sheets_default": "none, none",
            "media_default": "iso_a4_210x297mm",
            "sides_default": "two-sided-long-edge"
        }
    ],
    "jobs": [
        {
            "job_id": "1042",
            "job_printer_name": "WorkCentre-7835-Cor",
            "job_printer_uri": "/printers/WorkCentre-7835-Cor",
            "job_name": "Relatorio_Trimestral_Operacoes_2026.pdf",
            "job_originating_user_name": "m.silva",
            "job_k_octets": "4820",
            "job_impressions_completed": "3",
            "job_state": "5",  # 5=processing
            "date_time_at_creation": "2026-10-20 18:45:12",
            "date_time_at_processing": "2026-10-20 18:46:01",
            "date_time_at_completed": "",
            "job_printer_state_message": "Renderizando páginas 4-12",
            "job_preserved": "1"
        },
        {
            "job_id": "1041",
            "job_printer_name": "Zebra-GK420t-Etiquetas",
            "job_printer_uri": "/printers/Zebra-GK420t-Etiquetas",
            "job_name": "etiquetas_patrimonio_lote_88.zpl",
            "job_originating_user_name": "logistica",
            "job_k_octets": "14",
            "job_impressions_completed": "0",
            "job_state": "6",  # 6=stopped
            "date_time_at_creation": "2026-10-20 18:30:20",
            "date_time_at_processing": "2026-10-20 18:30:22",
            "date_time_at_completed": "",
            "job_printer_state_message": "Fila suspensa após erro de suprimento na impressora",
            "job_preserved": "1"
        },
        {
            "job_id": "1040",
            "job_printer_name": "LaserJet-Pro-M404dn",
            "job_printer_uri": "/printers/LaserJet-Pro-M404dn",
            "job_name": "Ordem_Servico_49182.pdf",
            "job_originating_user_name": "targino",
            "job_k_octets": "128",
            "job_impressions_completed": "2",
            "job_state": "9",  # 9=completed
            "date_time_at_creation": "2026-10-20 18:12:00",
            "date_time_at_processing": "2026-10-20 18:12:03",
            "date_time_at_completed": "2026-10-20 18:12:18",
            "job_printer_state_message": "",
            "job_preserved": "1"
        },
        {
            "job_id": "1039",
            "job_printer_name": "LaserJet-Pro-M404dn",
            "job_printer_uri": "/printers/LaserJet-Pro-M404dn",
            "job_name": "Manual_Operacao_Linux_v2.pdf",
            "job_originating_user_name": "f.costa",
            "job_k_octets": "940",
            "job_impressions_completed": "0",
            "job_state": "4",  # 4=held
            "date_time_at_creation": "2026-10-20 17:50:11",
            "date_time_at_processing": "",
            "date_time_at_completed": "",
            "job_printer_state_message": "Trabalho retido pelo operador para conferência de cota",
            "job_preserved": "1"
        }
    ],
    "classes": [
        {
            "printer_name": "Fila-Geral-Laser",
            "printer_info": "Pool balanceado de impressoras Laser A4",
            "printer_location": "Prédio Administrativo",
            "printer_uri_supported": "/classes/Fila-Geral-Laser",
            "member_uris": "LaserJet-Pro-M404dn",
            "printer_state": "3",
            "printer_state_message": "",
            "printer_is_accepting_jobs": "1",
            "printer_is_shared": "1",
            "server_is_sharing_printers": "1",
            "default_name": "LaserJet-Pro-M404dn"
        }
    ]
}

class CupsHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        try:
            self._handle_get()
        except Exception as e:
            import traceback
            traceback.print_exc()
            self.send_error(500, f"Server error: {e}")

    def _handle_get(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        query = urllib.parse.parse_qs(parsed.query)

        # Detect lang
        lang = query.get("lang", ["pt_BR"])[0]

        # 1. Static file handling from doc/
        if path.startswith("/images/") or path.startswith("/help/") or path in [
            "/cups.css", "/cups-printable.css", "/cups.js", "/apple-touch-icon.png", "/robots.txt"
        ]:
            rel_path = path.lstrip("/")
            file_path = os.path.join(DOC_DIR, rel_path)
            if os.path.isfile(file_path):
                self.serve_file(file_path)
                return
            else:
                self.send_error(404, f"File not found: {path}")
                return

        # 2. Template rendering
        engine = CupsTemplateEngine(TEMPLATES_DIR, DOC_DIR, lang)

        # Context builder
        base_ctx = {
            "ENV": {
                "CUPS_VERSION": MOCK_DATA["server"]["version"],
                "SERVER_NAME": MOCK_DATA["server"]["server_name"],
                "REMOTE_USER": MOCK_DATA["server"]["remote_user"],
                "REMOTE_NAME": MOCK_DATA["server"]["remote_name"],
                "CUPS_OAUTH_SERVER": ""
            },
            "COOKIES": {
                "org.cups.sid": MOCK_DATA["server"]["sid"]
            },
            "org.cups.sid": MOCK_DATA["server"]["sid"],
            "QUERY": query.get("QUERY", [""])[0],
            "alert_title": [],
            "alert_href": []
        }

        # Routes
        if path == "/" or path == "/index.html":
            base_ctx["SECTION"] = "home"
            base_ctx["title"] = "Início" if lang == "pt_BR" else "Home"
            header = engine.render_template("header.tmpl", base_ctx, lang)
            body = engine.render_template("home.tmpl", base_ctx, lang)
            trailer = engine.render_template("trailer.tmpl", base_ctx, lang)
            self.send_html(header + body + trailer)
            return

        elif path == "/printers" or path == "/printers/":
            base_ctx["SECTION"] = "printers"
            base_ctx["title"] = "Impressoras" if lang == "pt_BR" else "Printers"
            base_ctx["SEARCH_DEST"] = ""
            
            # Map printer array
            p_list = MOCK_DATA["printers"]
            filter_query = base_ctx["QUERY"].lower()
            if filter_query:
                p_list = [p for p in p_list if filter_query in p["printer_name"].lower() or filter_query in p["printer_info"].lower()]

            p_ctx = dict(base_ctx)
            p_ctx["printer_name"] = [p["printer_name"] for p in p_list]
            p_ctx["printer_info"] = [p["printer_info"] for p in p_list]
            p_ctx["printer_location"] = [p["printer_location"] for p in p_list]
            p_ctx["printer_make_and_model"] = [p["printer_make_and_model"] for p in p_list]
            p_ctx["printer_uri_supported"] = [p["printer_uri_supported"] for p in p_list]
            p_ctx["printer_state"] = [p["printer_state"] for p in p_list]
            p_ctx["printer_state_message"] = [p["printer_state_message"] for p in p_list]
            p_ctx["total"] = str(len(p_list))

            header = engine.render_template("header.tmpl", p_ctx, lang)
            search = engine.render_template("search.tmpl", p_ctx, lang)
            p_header = engine.render_template("printers-header.tmpl", p_ctx, lang)
            printers_body = engine.render_template("printers.tmpl", p_ctx, lang)
            trailer = engine.render_template("trailer.tmpl", p_ctx, lang)
            self.send_html(header + search + p_header + printers_body + trailer)
            return

        elif path.startswith("/printers/"):
            printer_name = path[len("/printers/"):]
            base_ctx["SECTION"] = "printers"
            base_ctx["title"] = printer_name

            # Find printer
            p_info = next((p for p in MOCK_DATA["printers"] if p["printer_name"] == printer_name), None)
            if not p_info:
                p_info = MOCK_DATA["printers"][0]

            single_ctx = dict(base_ctx)
            single_ctx.update(p_info)
            single_ctx["admin_uri"] = "/admin"

            # Jobs for this printer
            p_jobs = [j for j in MOCK_DATA["jobs"] if j["job_printer_name"] == p_info["printer_name"]]
            single_ctx["job_id"] = [j["job_id"] for j in p_jobs]
            single_ctx["job_name"] = [j["job_name"] for j in p_jobs]
            single_ctx["job_originating_user_name"] = [j["job_originating_user_name"] for j in p_jobs]
            single_ctx["job_k_octets"] = [j["job_k_octets"] for j in p_jobs]
            single_ctx["job_impressions_completed"] = [j["job_impressions_completed"] for j in p_jobs]
            single_ctx["job_state"] = [j["job_state"] for j in p_jobs]
            single_ctx["job_printer_name"] = [j["job_printer_name"] for j in p_jobs]
            single_ctx["job_printer_uri"] = [j["job_printer_uri"] for j in p_jobs]
            single_ctx["date_time_at_creation"] = [j["date_time_at_creation"] for j in p_jobs]
            single_ctx["date_time_at_processing"] = [j["date_time_at_processing"] for j in p_jobs]
            single_ctx["date_time_at_completed"] = [j["date_time_at_completed"] for j in p_jobs]
            single_ctx["job_printer_state_message"] = [j["job_printer_state_message"] for j in p_jobs]
            single_ctx["job_preserved"] = [j["job_preserved"] for j in p_jobs]

            header = engine.render_template("header.tmpl", single_ctx, lang)
            printer_body = engine.render_template("printer.tmpl", single_ctx, lang)
            p_jobs_header = engine.render_template("printer-jobs-header.tmpl", single_ctx, lang)
            jobs_body = engine.render_template("jobs.tmpl", single_ctx, lang)
            trailer = engine.render_template("trailer.tmpl", single_ctx, lang)
            self.send_html(header + printer_body + p_jobs_header + jobs_body + trailer)
            return

        elif path == "/jobs" or path == "/jobs/":
            base_ctx["SECTION"] = "jobs"
            base_ctx["title"] = "Trabalhos" if lang == "pt_BR" else "Jobs"
            base_ctx["which_jobs"] = query.get("which_jobs", [""])[0]

            j_list = MOCK_DATA["jobs"]
            if base_ctx["which_jobs"] == "completed":
                j_list = [j for j in j_list if j["job_state"] in ["7", "8", "9"]]
            elif base_ctx["which_jobs"] == "":
                j_list = [j for j in j_list if j["job_state"] not in ["7", "8", "9"]]

            j_ctx = dict(base_ctx)
            j_ctx["job_id"] = [j["job_id"] for j in j_list]
            j_ctx["job_name"] = [j["job_name"] for j in j_list]
            j_ctx["job_originating_user_name"] = [j["job_originating_user_name"] for j in j_list]
            j_ctx["job_k_octets"] = [j["job_k_octets"] for j in j_list]
            j_ctx["job_impressions_completed"] = [j["job_impressions_completed"] for j in j_list]
            j_ctx["job_state"] = [j["job_state"] for j in j_list]
            j_ctx["job_printer_name"] = [j["job_printer_name"] for j in j_list]
            j_ctx["job_printer_uri"] = [j["job_printer_uri"] for j in j_list]
            j_ctx["date_time_at_creation"] = [j["date_time_at_creation"] for j in j_list]
            j_ctx["date_time_at_processing"] = [j["date_time_at_processing"] for j in j_list]
            j_ctx["date_time_at_completed"] = [j["date_time_at_completed"] for j in j_list]
            j_ctx["job_printer_state_message"] = [j["job_printer_state_message"] for j in j_list]
            j_ctx["job_preserved"] = [j["job_preserved"] for j in j_list]

            header = engine.render_template("header.tmpl", j_ctx, lang)
            search = engine.render_template("search.tmpl", j_ctx, lang)
            j_header = engine.render_template("jobs-header.tmpl", j_ctx, lang)
            jobs_body = engine.render_template("jobs.tmpl", j_ctx, lang)
            trailer = engine.render_template("trailer.tmpl", j_ctx, lang)
            self.send_html(header + search + j_header + jobs_body + trailer)
            return

        elif path == "/classes" or path == "/classes/":
            base_ctx["SECTION"] = "classes"
            base_ctx["title"] = "Classes"
            c_list = MOCK_DATA["classes"]
            c_ctx = dict(base_ctx)
            c_ctx["printer_name"] = [c["printer_name"] for c in c_list]
            c_ctx["printer_info"] = [c["printer_info"] for c in c_list]
            c_ctx["printer_location"] = [c["printer_location"] for c in c_list]
            c_ctx["printer_uri_supported"] = [c["printer_uri_supported"] for c in c_list]
            c_ctx["member_uris"] = [c["member_uris"] for c in c_list]
            c_ctx["printer_state"] = [c["printer_state"] for c in c_list]
            c_ctx["printer_state_message"] = [c["printer_state_message"] for c in c_list]
            c_ctx["total"] = str(len(c_list))

            header = engine.render_template("header.tmpl", c_ctx, lang)
            search = engine.render_template("search.tmpl", c_ctx, lang)
            c_header = engine.render_template("classes-header.tmpl", c_ctx, lang)
            classes_body = engine.render_template("classes.tmpl", c_ctx, lang)
            trailer = engine.render_template("trailer.tmpl", c_ctx, lang)
            self.send_html(header + search + c_header + classes_body + trailer)
            return

        elif path == "/admin" or path == "/admin/":
            base_ctx["SECTION"] = "admin"
            base_ctx["title"] = "Administração" if lang == "pt_BR" else "Administration"
            admin_ctx = dict(base_ctx)
            admin_ctx.update(MOCK_DATA["server"])
            admin_ctx["ADVANCEDSETTINGS"] = query.get("ADVANCEDSETTINGS", [""])[0]

            header = engine.render_template("header.tmpl", admin_ctx, lang)
            admin_body = engine.render_template("admin.tmpl", admin_ctx, lang)
            trailer = engine.render_template("trailer.tmpl", admin_ctx, lang)
            self.send_html(header + admin_body + trailer)
            return

        elif path == "/error":
            base_ctx["SECTION"] = "error"
            base_ctx["title"] = "LaserJet-Pro-M404dn"
            base_ctx["printer_name"] = "LaserJet-Pro-M404dn"
            base_ctx["message"] = "Falha no backend de comunicação" if lang == "pt_BR" else "Backend communication error"
            base_ctx["error"] = "backend /usr/lib/cups/backend/socket failed: Connection timed out after 30 seconds"

            header = engine.render_template("header.tmpl", base_ctx, lang)
            err_body = engine.render_template("error.tmpl", base_ctx, lang)
            trailer = engine.render_template("trailer.tmpl", base_ctx, lang)
            self.send_html(header + err_body + trailer)
            return

        else:
            self.send_error(404, f"Page not found: {path}")

    def do_POST(self):
        # Mirror form submissions gracefully
        self.do_GET()

    def send_html(self, content: str):
        data = content.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
        self.end_headers()
        self.wfile.write(data)

    def serve_file(self, file_path: str):
        mime, _ = mimetypes.guess_type(file_path)
        if not mime:
            if file_path.endswith(".css"): mime = "text/css"
            elif file_path.endswith(".js"): mime = "application/javascript"
            elif file_path.endswith(".png"): mime = "image/png"
            elif file_path.endswith(".svg"): mime = "image/svg+xml"
            else: mime = "application/octet-stream"

        with open(file_path, "rb") as f:
            data = f.read()

        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(data)

def run_server():
    server = HTTPServer(("127.0.0.1", PORT), CupsHandler)
    print(f"CUPS Preview Server running at http://127.0.0.1:{PORT}/")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("Stopping preview server...")
        server.server_close()

if __name__ == "__main__":
    run_server()
