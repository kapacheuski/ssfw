import sys
import requests
import datetime
from PyQt5 import QtWidgets, QtGui, QtCore, QtWebEngineWidgets
import matplotlib
matplotlib.use("Qt5Agg")
from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg as FigureCanvas
import matplotlib.pyplot as plt

# --------- CONFIG ---------
API_KEY = "d1d45cef00f146a3b70ced76aac1a647"
CITY = "Minsk,by"
UNITS = "metric"  # or "imperial"
REFRESH_INTERVAL = 600  # seconds
# --------------------------

def get_weather(city, api_key, units="metric"):
    url = f"http://api.openweathermap.org/data/2.5/weather?q={city}&APPID={api_key}&units={units}"
    r = requests.get(url)
    r.raise_for_status()
    return r.json()

def get_forecast(city, api_key, units="metric"):
    url = f"http://api.openweathermap.org/data/2.5/forecast?q={city}&APPID={api_key}&units={units}"
    r = requests.get(url)
    r.raise_for_status()
    return r.json()

def weather_icon(icon_code):
    return f"http://openweathermap.org/img/wn/{icon_code}@2x.png"

class WeatherDashboard(QtWidgets.QWidget):
    def __init__(self, city, api_key, units):
        super().__init__()
        self.city = city
        self.api_key = api_key
        self.units = units
        self.init_ui()
        self.refresh_weather()
        self.timer = QtCore.QTimer()
        self.timer.timeout.connect(self.refresh_weather)
        self.timer.start(REFRESH_INTERVAL * 1000)

    def init_ui(self):
        self.setWindowTitle("Weather Dashboard")
        self.setGeometry(200, 200, 700, 700)  # More compact window
        # Dark theme styling
        self.setStyleSheet("""
            QWidget {
                background-color: #23272f;
                color: #f8f8f2;
                font-family: Arial;
            }
            QLabel {
                color: #f8f8f2;
            }
        """)

        self.layout = QtWidgets.QVBoxLayout(self)
        self.layout.setSpacing(6)
        self.layout.setContentsMargins(10, 10, 10, 10)

        self.city_label = QtWidgets.QLabel(self.city)
        self.city_label.setAlignment(QtCore.Qt.AlignCenter)
        self.city_label.setStyleSheet("font-size: 20px; font-weight: bold; color: #40a9ff; margin-bottom: 2px;")
        self.layout.addWidget(self.city_label)

        self.icon_temp_row = QtWidgets.QHBoxLayout()
        self.icon_label = QtWidgets.QLabel()
        self.icon_label.setAlignment(QtCore.Qt.AlignCenter)
        self.icon_label.setFixedSize(60, 60)
        self.icon_temp_row.addWidget(self.icon_label)

        self.temp_label = QtWidgets.QLabel("--°C")
        self.temp_label.setAlignment(QtCore.Qt.AlignVCenter)
        self.temp_label.setStyleSheet("font-size: 32px; font-weight: bold; color: #ffb74d; margin-left: 8px;")
        self.icon_temp_row.addWidget(self.temp_label)
        self.icon_temp_row.addStretch()
        self.layout.addLayout(self.icon_temp_row)

        self.desc_label = QtWidgets.QLabel("")
        self.desc_label.setAlignment(QtCore.Qt.AlignCenter)
        self.desc_label.setStyleSheet("font-size: 14px; color: #bdbdbd; margin-bottom: 2px;")
        self.layout.addWidget(self.desc_label)

        self.details_label = QtWidgets.QLabel("")
        self.details_label.setAlignment(QtCore.Qt.AlignCenter)
        self.details_label.setStyleSheet("font-size: 12px; color: #90caf9;")
        self.layout.addWidget(self.details_label)

        self.time_label = QtWidgets.QLabel("")
        self.time_label.setAlignment(QtCore.Qt.AlignCenter)
        self.time_label.setStyleSheet("font-size: 11px; color: #bdbdbd;")
        self.layout.addWidget(self.time_label)

        # Hourly forecast section (no scrollbar, just a horizontal layout)
        self.hourly_title = QtWidgets.QLabel("Hourly Forecast (Next 12h)")
        self.hourly_title.setAlignment(QtCore.Qt.AlignCenter)
        self.hourly_title.setStyleSheet("font-size: 14px; font-weight: bold; color: #40a9ff; margin-top: 4px;")
        self.layout.addWidget(self.hourly_title)

        self.hourly_widget = QtWidgets.QWidget()
        self.hourly_layout = QtWidgets.QHBoxLayout(self.hourly_widget)
        self.hourly_layout.setSpacing(4)
        self.hourly_widgets = []
        for _ in range(12):
            vbox = QtWidgets.QVBoxLayout()
            vbox.setSpacing(2)
            time_label = QtWidgets.QLabel("--:--")
            time_label.setAlignment(QtCore.Qt.AlignCenter)
            time_label.setStyleSheet("font-size: 10px;")
            icon_label = QtWidgets.QLabel()
            icon_label.setAlignment(QtCore.Qt.AlignCenter)
            icon_label.setFixedSize(20, 20)  # Smaller icon size
            temp_label = QtWidgets.QLabel("--°C")
            temp_label.setAlignment(QtCore.Qt.AlignCenter)
            temp_label.setStyleSheet("font-size: 11px; color: #ffb74d;")
            vbox.addWidget(time_label)
            vbox.addWidget(icon_label)
            vbox.addWidget(temp_label)
            container = QtWidgets.QWidget()
            container.setLayout(vbox)
            self.hourly_layout.addWidget(container)
            self.hourly_widgets.append((time_label, icon_label, temp_label))
        self.layout.addWidget(self.hourly_widget)

        # --- Temperature chart for hourly forecast ---
        self.temp_chart_title = QtWidgets.QLabel("Hourly Temperature Trend")
        self.temp_chart_title.setAlignment(QtCore.Qt.AlignCenter)
        self.temp_chart_title.setStyleSheet("font-size: 13px; font-weight: bold; color: #40a9ff; margin-top: 4px;")
        self.layout.addWidget(self.temp_chart_title)

        self.temp_fig, self.temp_ax = plt.subplots(figsize=(5, 1.2), dpi=100)
        self.temp_canvas = FigureCanvas(self.temp_fig)
        self.temp_canvas.setMinimumHeight(90)
        self.temp_canvas.setMaximumHeight(110)
        self.temp_ax.set_facecolor("#23272f")
        self.temp_fig.patch.set_facecolor("#23272f")
        self.temp_ax.tick_params(axis='x', colors='#f8f8f2', labelsize=8)
        self.temp_ax.tick_params(axis='y', colors='#f8f8f2', labelsize=8)
        self.temp_ax.spines['bottom'].set_color('#f8f8f2')
        self.temp_ax.spines['top'].set_color('#23272f')
        self.temp_ax.spines['right'].set_color('#23272f')
        self.temp_ax.spines['left'].set_color('#f8f8f2')
        self.temp_ax.set_ylabel("°C", color="#f8f8f2", fontsize=9)
        self.temp_ax.set_xlabel("Hour", color="#f8f8f2", fontsize=9)
        self.temp_line, = self.temp_ax.plot([], [], marker="o", color="#ffb74d")
        self.layout.addWidget(self.temp_canvas)

        # Forecast section
        self.forecast_title = QtWidgets.QLabel("7-Day Forecast")
        self.forecast_title.setAlignment(QtCore.Qt.AlignCenter)
        self.forecast_title.setStyleSheet("font-size: 15px; font-weight: bold; margin-top: 4px; color: #40a9ff;")
        self.layout.addWidget(self.forecast_title)

        self.forecast_layout = QtWidgets.QHBoxLayout()
        self.forecast_layout.setSpacing(4)
        self.forecast_widgets = []
        for _ in range(7):
            vbox = QtWidgets.QVBoxLayout()
            vbox.setSpacing(2)
            day_label = QtWidgets.QLabel("--")
            day_label.setAlignment(QtCore.Qt.AlignCenter)
            day_label.setStyleSheet("font-size: 11px; color: #40a9ff;")
            icon_label = QtWidgets.QLabel()
            icon_label.setAlignment(QtCore.Qt.AlignCenter)
            icon_label.setFixedSize(20, 20)  # Smaller icon size for forecast
            temp_label = QtWidgets.QLabel("--°C")
            temp_label.setAlignment(QtCore.Qt.AlignCenter)
            temp_label.setStyleSheet("font-size: 11px; color: #ffb74d;")
            vbox.addWidget(day_label)
            vbox.addWidget(icon_label)
            vbox.addWidget(temp_label)
            container = QtWidgets.QWidget()
            container.setLayout(vbox)
            self.forecast_layout.addWidget(container)
            self.forecast_widgets.append((day_label, icon_label, temp_label))
        self.layout.addLayout(self.forecast_layout)

        # Map view
        self.map_label = QtWidgets.QLabel("Map")
        self.map_label.setAlignment(QtCore.Qt.AlignCenter)
        self.map_label.setStyleSheet("font-size: 13px; font-weight: bold; color: #40a9ff; margin-top: 4px;")
        self.layout.addWidget(self.map_label)

        self.map_view = QtWebEngineWidgets.QWebEngineView()
        self.map_view.setMinimumHeight(120)
        self.map_view.setMaximumHeight(180)
        self.layout.addWidget(self.map_view)

    def refresh_weather(self):
        try:
            data = get_weather(self.city, self.api_key, self.units)
            temp = data["main"]["temp"]
            desc = data["weather"][0]["description"].capitalize()
            icon = data["weather"][0]["icon"]
            humidity = data["main"]["humidity"]
            wind = data["wind"]["speed"]
            pressure = data["main"]["pressure"]
            dt = datetime.datetime.fromtimestamp(data["dt"])
            lat = data["coord"]["lat"]
            lon = data["coord"]["lon"]
            self.temp_label.setText(f"{temp:.1f}°{'C' if self.units=='metric' else 'F'}")
            self.desc_label.setText(desc)
            self.details_label.setText(f"Humidity: {humidity}%  |  Wind: {wind} m/s  |  Pressure: {pressure} hPa")
            self.time_label.setText(f"Updated: {dt.strftime('%Y-%m-%d %H:%M:%S')}")
            # Load icon
            pixmap = QtGui.QPixmap()
            pixmap.loadFromData(requests.get(weather_icon(icon)).content)
            self.icon_label.setPixmap(pixmap)
            # Update hourly forecast
            self.refresh_hourly()
            # Update forecast
            self.refresh_forecast()
            # Update map
            self.update_map(lat, lon)
        except Exception as e:
            self.temp_label.setText("--°C")
            self.desc_label.setText("Error loading weather")
            self.details_label.setText(str(e))

    def refresh_hourly(self):
        try:
            forecast = get_forecast(self.city, self.api_key, self.units)
            now = datetime.datetime.now()
            count = 0
            hours = []
            temps = []
            for entry in forecast["list"]:
                dt = datetime.datetime.fromtimestamp(entry["dt"])
                if dt < now:
                    continue
                if count >= 12:
                    break
                temp = entry["main"]["temp"]
                icon = entry["weather"][0]["icon"]
                time_label, icon_label, temp_label = self.hourly_widgets[count]
                time_label.setText(dt.strftime("%H:%M"))
                pixmap = QtGui.QPixmap()
                pixmap.loadFromData(requests.get(weather_icon(icon)).content)
                # Scale pixmap to fit the smaller icon label
                pixmap = pixmap.scaled(20, 20, QtCore.Qt.KeepAspectRatio, QtCore.Qt.SmoothTransformation)
                icon_label.setPixmap(pixmap)
                temp_label.setText(f"{temp:.1f}°{'C' if self.units=='metric' else 'F'}")
                hours.append(dt.strftime("%H"))
                temps.append(temp)
                count += 1
            # Fill remaining slots if less than 12
            for i in range(count, 12):
                time_label, icon_label, temp_label = self.hourly_widgets[i]
                time_label.setText("--:--")
                icon_label.clear()
                temp_label.setText("--°C")
            # --- Update temperature chart ---
            self.temp_line.set_data(range(len(temps)), temps)
            self.temp_ax.set_xticks(range(len(hours)))
            self.temp_ax.set_xticklabels(hours)
            if temps:
                self.temp_ax.set_ylim(min(temps) - 2, max(temps) + 2)
            self.temp_ax.set_xlim(-0.5, max(len(temps) - 0.5, 11.5))
            self.temp_canvas.draw()
        except Exception as e:
            for time_label, icon_label, temp_label in self.hourly_widgets:
                time_label.setText("--:--")
                icon_label.clear()
                temp_label.setText("--°C")
            # Clear chart
            self.temp_line.set_data([], [])
            self.temp_ax.set_xticks([])
            self.temp_ax.set_xticklabels([])
            self.temp_canvas.draw()

    def refresh_forecast(self):
        try:
            forecast = get_forecast(self.city, self.api_key, self.units)
            # Group forecast by day and by day/night
            days = {}
            for entry in forecast["list"]:
                dt = datetime.datetime.fromtimestamp(entry["dt"])
                day = dt.date()
                hour = dt.hour
                temp = entry["main"]["temp"]
                icon = entry["weather"][0]["icon"]
                # Daytime: 6-18, Night: 18-6
                is_day = 6 <= hour < 18
                if day not in days:
                    days[day] = {"day_temps": [], "night_temps": [], "day_icons": [], "night_icons": []}
                if is_day:
                    days[day]["day_temps"].append(temp)
                    days[day]["day_icons"].append(icon)
                else:
                    days[day]["night_temps"].append(temp)
                    days[day]["night_icons"].append(icon)
            # Sort and pick up to 7 days
            sorted_days = sorted(days.keys())
            for i in range(7):
                if i < len(sorted_days):
                    day = sorted_days[i]
                    day_temps = days[day]["day_temps"]
                    night_temps = days[day]["night_temps"]
                    day_icons = days[day]["day_icons"]
                    night_icons = days[day]["night_icons"]
                    # Average temps, fallback to '--' if missing
                    if day_temps:
                        avg_day_temp = sum(day_temps) / len(day_temps)
                        day_icon = max(set(day_icons), key=day_icons.count)
                    else:
                        avg_day_temp = None
                        day_icon = None
                    if night_temps:
                        avg_night_temp = sum(night_temps) / len(night_temps)
                        night_icon = max(set(night_icons), key=night_icons.count)
                    else:
                        avg_night_temp = None
                        night_icon = None
                    day_label, icon_label, temp_label = self.forecast_widgets[i]
                    day_label.setText(day.strftime("%a"))
                    # Show both day and night icons if available, else just one
                    if day_icon and night_icon and day_icon != night_icon:
                        # Combine icons horizontally and scale
                        day_pixmap = QtGui.QPixmap()
                        day_pixmap.loadFromData(requests.get(weather_icon(day_icon)).content)
                        day_pixmap = day_pixmap.scaled(10, 20, QtCore.Qt.KeepAspectRatio, QtCore.Qt.SmoothTransformation)
                        night_pixmap = QtGui.QPixmap()
                        night_pixmap.loadFromData(requests.get(weather_icon(night_icon)).content)
                        night_pixmap = night_pixmap.scaled(10, 20, QtCore.Qt.KeepAspectRatio, QtCore.Qt.SmoothTransformation)
                        combined = QtGui.QPixmap(20, 20)
                        combined.fill(QtCore.Qt.transparent)
                        painter = QtGui.QPainter(combined)
                        painter.drawPixmap(0, 0, day_pixmap)
                        painter.drawPixmap(10, 0, night_pixmap)
                        painter.end()
                        icon_label.setPixmap(combined)
                    elif day_icon:
                        pixmap = QtGui.QPixmap()
                        pixmap.loadFromData(requests.get(weather_icon(day_icon)).content)
                        pixmap = pixmap.scaled(20, 20, QtCore.Qt.KeepAspectRatio, QtCore.Qt.SmoothTransformation)
                        icon_label.setPixmap(pixmap)
                    elif night_icon:
                        pixmap = QtGui.QPixmap()
                        pixmap.loadFromData(requests.get(weather_icon(night_icon)).content)
                        pixmap = pixmap.scaled(20, 20, QtCore.Qt.KeepAspectRatio, QtCore.Qt.SmoothTransformation)
                        icon_label.setPixmap(pixmap)
                    else:
                        icon_label.clear()
                    # Show both day and night temps
                    t_day = f"{avg_day_temp:.1f}°" if avg_day_temp is not None else "--"
                    t_night = f"{avg_night_temp:.1f}°" if avg_night_temp is not None else "--"
                    temp_label.setText(f"Day: {t_day}\nNight: {t_night}")
                else:
                    day_label, icon_label, temp_label = self.forecast_widgets[i]
                    day_label.setText("--")
                    icon_label.clear()
                    temp_label.setText("--°C\n--°C")
        except Exception as e:
            for day_label, icon_label, temp_label in self.forecast_widgets:
                day_label.setText("--")
                icon_label.clear()
                temp_label.setText("--°C\n--°C")

    def update_map(self, lat, lon):
        # Use OpenStreetMap with a marker
        html = f"""
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="utf-8" />
            <style>
                html, body, #map {{ height: 100%; margin: 0; padding: 0; background: #23272f; }}
            </style>
            <link rel="stylesheet" href="https://unpkg.com/leaflet/dist/leaflet.css" />
            <script src="https://unpkg.com/leaflet/dist/leaflet.js"></script>
        </head>
        <body>
            <div id="map"></div>
            <script>
                var map = L.map('map').setView([{lat}, {lon}], 10);
                L.tileLayer('https://{{s}}.tile.openstreetmap.org/{{z}}/{{x}}/{{y}}.png', {{
                    maxZoom: 19,
                    attribution: '© OpenStreetMap'
                }}).addTo(map);
                var marker = L.marker([{lat}, {lon}]).addTo(map);
            </script>
        </body>
        </html>
        """
        self.map_view.setHtml(html)

if __name__ == "__main__":
    app = QtWidgets.QApplication(sys.argv)
    dashboard = WeatherDashboard(CITY, API_KEY, UNITS)
    dashboard.show()
    sys.exit(app.exec_())