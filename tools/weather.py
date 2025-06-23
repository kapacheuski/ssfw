import sys
import requests
import datetime
from PyQt5 import QtWidgets, QtGui, QtCore, QtWebEngineWidgets

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
        self.setGeometry(200, 200, 800, 700)
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

        self.city_label = QtWidgets.QLabel(self.city)
        self.city_label.setAlignment(QtCore.Qt.AlignCenter)
        self.city_label.setStyleSheet("font-size: 28px; font-weight: bold; color: #40a9ff;")
        self.layout.addWidget(self.city_label)

        self.icon_label = QtWidgets.QLabel()
        self.icon_label.setAlignment(QtCore.Qt.AlignCenter)
        self.layout.addWidget(self.icon_label)

        self.temp_label = QtWidgets.QLabel("--°C")
        self.temp_label.setAlignment(QtCore.Qt.AlignCenter)
        self.temp_label.setStyleSheet("font-size: 48px; font-weight: bold; color: #ffb74d;")
        self.layout.addWidget(self.temp_label)

        self.desc_label = QtWidgets.QLabel("")
        self.desc_label.setAlignment(QtCore.Qt.AlignCenter)
        self.desc_label.setStyleSheet("font-size: 20px; color: #bdbdbd;")
        self.layout.addWidget(self.desc_label)

        self.details_label = QtWidgets.QLabel("")
        self.details_label.setAlignment(QtCore.Qt.AlignCenter)
        self.details_label.setStyleSheet("font-size: 16px; color: #90caf9;")
        self.layout.addWidget(self.details_label)

        self.time_label = QtWidgets.QLabel("")
        self.time_label.setAlignment(QtCore.Qt.AlignCenter)
        self.time_label.setStyleSheet("font-size: 14px; color: #bdbdbd;")
        self.layout.addWidget(self.time_label)

        # Forecast section
        self.forecast_title = QtWidgets.QLabel("7-Day Forecast")
        self.forecast_title.setAlignment(QtCore.Qt.AlignCenter)
        self.forecast_title.setStyleSheet("font-size: 22px; font-weight: bold; margin-top: 10px; color: #40a9ff;")
        self.layout.addWidget(self.forecast_title)

        self.forecast_layout = QtWidgets.QHBoxLayout()
        self.forecast_widgets = []
        for _ in range(7):
            vbox = QtWidgets.QVBoxLayout()
            day_label = QtWidgets.QLabel("--")
            day_label.setAlignment(QtCore.Qt.AlignCenter)
            day_label.setStyleSheet("font-size: 16px; color: #40a9ff;")
            icon_label = QtWidgets.QLabel()
            icon_label.setAlignment(QtCore.Qt.AlignCenter)
            temp_label = QtWidgets.QLabel("--°C")
            temp_label.setAlignment(QtCore.Qt.AlignCenter)
            temp_label.setStyleSheet("font-size: 16px; color: #ffb74d;")
            vbox.addWidget(day_label)
            vbox.addWidget(icon_label)
            vbox.addWidget(temp_label)
            self.forecast_layout.addLayout(vbox)
            self.forecast_widgets.append((day_label, icon_label, temp_label))
        self.layout.addLayout(self.forecast_layout)

        # Map view
        self.map_label = QtWidgets.QLabel("Map")
        self.map_label.setAlignment(QtCore.Qt.AlignCenter)
        self.map_label.setStyleSheet("font-size: 20px; font-weight: bold; color: #40a9ff; margin-top: 10px;")
        self.layout.addWidget(self.map_label)

        self.map_view = QtWebEngineWidgets.QWebEngineView()
        self.map_view.setMinimumHeight(250)
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
            # Update forecast
            self.refresh_forecast()
            # Update map
            self.update_map(lat, lon)
        except Exception as e:
            self.temp_label.setText("--°C")
            self.desc_label.setText("Error loading weather")
            self.details_label.setText(str(e))

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
                        # Combine icons horizontally
                        day_pixmap = QtGui.QPixmap()
                        day_pixmap.loadFromData(requests.get(weather_icon(day_icon)).content)
                        night_pixmap = QtGui.QPixmap()
                        night_pixmap.loadFromData(requests.get(weather_icon(night_icon)).content)
                        combined = QtGui.QPixmap(day_pixmap.width() + night_pixmap.width(), day_pixmap.height())
                        combined.fill(QtCore.Qt.transparent)
                        painter = QtGui.QPainter(combined)
                        painter.drawPixmap(0, 0, day_pixmap)
                        painter.drawPixmap(day_pixmap.width(), 0, night_pixmap)
                        painter.end()
                        icon_label.setPixmap(combined)
                    elif day_icon:
                        pixmap = QtGui.QPixmap()
                        pixmap.loadFromData(requests.get(weather_icon(day_icon)).content)
                        icon_label.setPixmap(pixmap)
                    elif night_icon:
                        pixmap = QtGui.QPixmap()
                        pixmap.loadFromData(requests.get(weather_icon(night_icon)).content)
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