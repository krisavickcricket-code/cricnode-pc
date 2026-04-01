#include "CricNodeOverlayManager.hpp"
#include "OBSBasic.hpp"

#include <dialogs/CricNodeFixtureBrowser.hpp>
#include <utility/CricNodeFixtureMonitor.hpp>

#include <qt-wrappers.hpp>

#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QUuid>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>

#include <obs-frontend-api.h>

/* ---- CricNodeOverlay JSON serialization ---- */

QJsonObject CricNodeOverlay::toJson() const
{
	QJsonObject obj;
	obj["id"] = QString::fromStdString(id);
	obj["name"] = QString::fromStdString(name);
	obj["type"] = QString::fromStdString(type);
	obj["enabled"] = enabled;
	obj["imagePath"] = QString::fromStdString(imagePath);
	obj["url"] = QString::fromStdString(url);
	obj["scorecardProvider"] = QString::fromStdString(scorecardProvider);
	obj["matchId"] = QString::fromStdString(matchId);
	obj["clubId"] = QString::fromStdString(clubId);
	obj["team1"] = QString::fromStdString(team1);
	obj["team2"] = QString::fromStdString(team2);
	obj["matchDate"] = QString::fromStdString(matchDate);
	obj["rotationIntervalSec"] = rotationIntervalSec;
	obj["screenX"] = (double)screenX;
	obj["screenY"] = (double)screenY;
	obj["screenW"] = (double)screenW;
	obj["screenH"] = (double)screenH;

	QJsonArray paths;
	for (auto &p : rotationImagePaths)
		paths.append(QString::fromStdString(p));
	obj["rotationImagePaths"] = paths;

	return obj;
}

CricNodeOverlay CricNodeOverlay::fromJson(const QJsonObject &obj)
{
	CricNodeOverlay o;
	o.id = obj["id"].toString().toStdString();
	o.name = obj["name"].toString().toStdString();
	o.type = obj["type"].toString().toStdString();
	o.enabled = obj["enabled"].toBool(true);
	o.imagePath = obj["imagePath"].toString().toStdString();
	o.url = obj["url"].toString().toStdString();
	o.scorecardProvider = obj["scorecardProvider"].toString().toStdString();
	o.matchId = obj["matchId"].toString().toStdString();
	o.clubId = obj["clubId"].toString().toStdString();
	o.team1 = obj["team1"].toString().toStdString();
	o.team2 = obj["team2"].toString().toStdString();
	o.matchDate = obj["matchDate"].toString().toStdString();
	o.rotationIntervalSec = obj["rotationIntervalSec"].toInt(5);
	o.screenX = (float)obj["screenX"].toDouble(0.0);
	o.screenY = (float)obj["screenY"].toDouble(0.0);
	o.screenW = (float)obj["screenW"].toDouble(1.0);
	o.screenH = (float)obj["screenH"].toDouble(1.0);

	QJsonArray paths = obj["rotationImagePaths"].toArray();
	for (auto v : paths)
		o.rotationImagePaths.push_back(v.toString().toStdString());

	return o;
}

/* ---- CricNodeOverlayManager widget ---- */

CricNodeOverlayManager::CricNodeOverlayManager(QWidget *parent)
	: QWidget(parent)
{
	mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(4, 4, 4, 4);

	/* Header */
	QLabel *header = new QLabel("Overlays", this);
	header->setStyleSheet("font-weight: bold; font-size: 13px;");
	mainLayout->addWidget(header);

	/* List */
	overlayList = new QListWidget(this);
	overlayList->setAlternatingRowColors(true);
	mainLayout->addWidget(overlayList);

	connect(overlayList, &QListWidget::itemDoubleClicked, this, &CricNodeOverlayManager::EditOverlay);

	/* Buttons */
	QHBoxLayout *btnLayout = new QHBoxLayout();

	addButton = new QPushButton("+", this);
	addButton->setToolTip("Add Overlay");
	addButton->setMaximumWidth(32);
	connect(addButton, &QPushButton::clicked, this, &CricNodeOverlayManager::AddOverlayClicked);

	editButton = new QPushButton("Edit", this);
	editButton->setToolTip("Edit selected overlay");
	connect(editButton, &QPushButton::clicked, this, &CricNodeOverlayManager::EditOverlay);

	removeButton = new QPushButton("-", this);
	removeButton->setToolTip("Remove selected overlay");
	removeButton->setMaximumWidth(32);
	connect(removeButton, &QPushButton::clicked, this, &CricNodeOverlayManager::RemoveOverlay);

	btnLayout->addWidget(addButton);
	btnLayout->addWidget(editButton);
	btnLayout->addStretch();
	btnLayout->addWidget(removeButton);
	mainLayout->addLayout(btnLayout);

	/* Rotation timer for image rotation overlays */
	rotationTimer = new QTimer(this);
	rotationTimer->setInterval(1000);
	connect(rotationTimer, &QTimer::timeout, this, &CricNodeOverlayManager::UpdateRotatingImages);
	rotationTimer->start();

	/* Connect fixture monitor for auto-switch to live */
	connect(CricNodeFixtureMonitor::Instance(),
		&CricNodeFixtureMonitor::FixtureWentLive, this,
		&CricNodeOverlayManager::OnFixtureWentLive);

	LoadConfig();
}

CricNodeOverlayManager::~CricNodeOverlayManager()
{
	CricNodeFixtureMonitor::Instance()->StopAll();
	SaveConfig();
}

std::string CricNodeOverlayManager::GenerateId()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

void CricNodeOverlayManager::AddOverlayClicked()
{
	QMenu menu(this);
	menu.addAction("Image", this, &CricNodeOverlayManager::AddImageOverlay);
	menu.addAction("Image Rotation (2-10 images)", this, &CricNodeOverlayManager::AddImageRotationOverlay);
	menu.addAction("Webpage (URL)", this, &CricNodeOverlayManager::AddUrlOverlay);
	menu.addAction("League Scorecard", this, &CricNodeOverlayManager::AddScorecardOverlay);
	menu.exec(addButton->mapToGlobal(QPoint(0, addButton->height())));
}

void CricNodeOverlayManager::AddImageOverlay()
{
	QString file = QFileDialog::getOpenFileName(this, "Select Image", "",
						    "Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)");
	if (file.isEmpty())
		return;

	bool ok;
	QString name = QInputDialog::getText(this, "Overlay Name", "Name:", QLineEdit::Normal,
					     QFileInfo(file).baseName(), &ok);
	if (!ok || name.isEmpty())
		return;

	CricNodeOverlay overlay;
	overlay.id = GenerateId();
	overlay.name = name.toStdString();
	overlay.type = "image";
	overlay.imagePath = file.toStdString();

	overlays.push_back(overlay);
	CreateOrUpdateSource(overlay);
	RefreshList();
	SaveConfig();
	emit OverlaysChanged();
}

void CricNodeOverlayManager::AddImageRotationOverlay()
{
	QStringList files = QFileDialog::getOpenFileNames(this, "Select Images (2-10)", "",
							  "Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)");
	if (files.size() < 2) {
		if (!files.isEmpty())
			QMessageBox::warning(this, "Image Rotation", "Please select at least 2 images.");
		return;
	}
	if (files.size() > 10) {
		QMessageBox::warning(this, "Image Rotation", "Maximum 10 images allowed.");
		return;
	}

	bool ok;
	int interval = QInputDialog::getInt(this, "Rotation Interval", "Seconds between images:", 5, 2, 30, 1, &ok);
	if (!ok)
		return;

	QString name = QInputDialog::getText(this, "Overlay Name", "Name:", QLineEdit::Normal,
					     QString("Rotation (%1 images)").arg(files.size()), &ok);
	if (!ok || name.isEmpty())
		return;

	CricNodeOverlay overlay;
	overlay.id = GenerateId();
	overlay.name = name.toStdString();
	overlay.type = "image_rotation";
	overlay.imagePath = files[0].toStdString();
	overlay.rotationIntervalSec = interval;
	for (auto &f : files)
		overlay.rotationImagePaths.push_back(f.toStdString());

	overlays.push_back(overlay);
	CreateOrUpdateSource(overlay);
	RefreshList();
	SaveConfig();
	emit OverlaysChanged();
}

void CricNodeOverlayManager::AddUrlOverlay()
{
	bool ok;
	QString url = QInputDialog::getText(this, "Webpage URL", "Enter URL (https://):", QLineEdit::Normal,
					    "https://", &ok);
	if (!ok || url.isEmpty())
		return;

	QString name =
		QInputDialog::getText(this, "Overlay Name", "Name:", QLineEdit::Normal, "Web Overlay", &ok);
	if (!ok || name.isEmpty())
		return;

	CricNodeOverlay overlay;
	overlay.id = GenerateId();
	overlay.name = name.toStdString();
	overlay.type = "url";
	overlay.url = url.toStdString();

	overlays.push_back(overlay);
	CreateOrUpdateSource(overlay);
	RefreshList();
	SaveConfig();
	emit OverlaysChanged();
}

void CricNodeOverlayManager::AddScorecardOverlay()
{
	/* Ask user: browse fixtures or enter manually? */
	QStringList options;
	options << "Browse Fixtures (recommended)" << "Enter Match ID Manually";

	bool ok;
	QString choice = QInputDialog::getItem(this, "Add Scorecard",
					       "How would you like to add a scorecard?",
					       options, 0, false, &ok);
	if (!ok)
		return;

	CricNodeOverlay overlay;
	overlay.id = GenerateId();
	overlay.type = "scorecard";

	if (choice == options[0]) {
		/* Launch fixture browser dialog */
		CricNodeFixtureBrowser browser(this);
		if (browser.exec() != QDialog::Accepted)
			return;

		CricNodeFixture fixture = browser.GetSelectedFixture();
		if (fixture.matchId.empty())
			return;

		overlay.scorecardProvider = fixture.provider;
		overlay.matchId = fixture.matchId;
		overlay.clubId = fixture.clubId;
		overlay.team1 = fixture.team1;
		overlay.team2 = fixture.team2;
		overlay.matchDate = fixture.date;

		QString providerName;
		if (fixture.provider == "cricclubs")
			providerName = "CricClubs";
		else if (fixture.provider == "dcl")
			providerName = "DCL";
		else if (fixture.provider == "playcricket")
			providerName = "Play-Cricket";
		else if (fixture.provider == "playhq")
			providerName = "PlayHQ";

		overlay.name = providerName.toStdString() + " - " +
			       fixture.team1 + " vs " + fixture.team2;

		/* If the fixture is scheduled (not live), start monitoring */
		if (fixture.status == "SCHEDULED" &&
		    (fixture.provider == "dcl" || fixture.provider == "cricclubs")) {
			MonitoredFixture mf;
			mf.overlayId = overlay.id;
			mf.provider = fixture.provider;
			mf.matchId = fixture.matchId;
			mf.clubId = fixture.clubId;
			mf.team1 = fixture.team1;
			mf.team2 = fixture.team2;
			mf.matchDate = fixture.date;
			CricNodeFixtureMonitor::Instance()->StartMonitoring(mf);
		}
	} else {
		/* Manual entry fallback */
		QStringList providers;
		providers << "CricClubs" << "Play-Cricket" << "PlayHQ" << "DCL";

		QString provider = QInputDialog::getItem(
			this, "Scorecard Provider",
			"Select league provider:", providers, 0, false, &ok);
		if (!ok)
			return;

		QString matchId = QInputDialog::getText(
			this, "Match ID", "Enter Match ID:",
			QLineEdit::Normal, "", &ok);
		if (!ok || matchId.isEmpty())
			return;

		if (provider == "CricClubs") {
			overlay.scorecardProvider = "cricclubs";
			QString clubId = QInputDialog::getText(
				this, "Club ID", "Enter Club ID:",
				QLineEdit::Normal, "", &ok);
			if (!ok)
				return;
			overlay.clubId = clubId.toStdString();
		} else if (provider == "Play-Cricket") {
			overlay.scorecardProvider = "playcricket";
		} else if (provider == "PlayHQ") {
			overlay.scorecardProvider = "playhq";
		} else if (provider == "DCL") {
			overlay.scorecardProvider = "dcl";
		}

		overlay.matchId = matchId.toStdString();
		overlay.name = provider.toStdString() + " - Match " +
			       matchId.toStdString();
	}

	overlays.push_back(overlay);
	CreateOrUpdateSource(overlay);
	RefreshList();
	SaveConfig();
	emit OverlaysChanged();
}

void CricNodeOverlayManager::EditOverlay()
{
	int row = overlayList->currentRow();
	if (row < 0 || row >= (int)overlays.size())
		return;

	CricNodeOverlay &overlay = overlays[row];

	bool ok;
	QString name = QInputDialog::getText(this, "Edit Overlay", "Name:", QLineEdit::Normal,
					     QString::fromStdString(overlay.name), &ok);
	if (ok && !name.isEmpty()) {
		overlay.name = name.toStdString();
		CreateOrUpdateSource(overlay);
		RefreshList();
		SaveConfig();
		emit OverlaysChanged();
	}
}

void CricNodeOverlayManager::RemoveOverlay()
{
	int row = overlayList->currentRow();
	if (row < 0 || row >= (int)overlays.size())
		return;

	/* Stop fixture monitoring if active for this overlay */
	CricNodeFixtureMonitor::Instance()->StopMonitoring(overlays[row].id);

	RemoveSourceForOverlay(overlays[row].id);
	overlays.erase(overlays.begin() + row);
	RefreshList();
	SaveConfig();
	emit OverlaysChanged();
}

void CricNodeOverlayManager::ToggleOverlay(int row)
{
	if (row < 0 || row >= (int)overlays.size())
		return;

	overlays[row].enabled = !overlays[row].enabled;

	/* Find the scene item and toggle visibility */
	OBSBasic *main = OBSBasic::Get();
	OBSScene scene = main->GetCurrentScene();
	if (!scene)
		return;

	std::string srcName = "CricNode_" + overlays[row].id;
	OBSSourceAutoRelease source = obs_get_source_by_name(srcName.c_str());
	if (source) {
		obs_sceneitem_t *item = obs_scene_find_source(scene, srcName.c_str());
		if (item)
			obs_sceneitem_set_visible(item, overlays[row].enabled);
	}

	RefreshList();
	SaveConfig();
}

void CricNodeOverlayManager::RefreshList()
{
	overlayList->clear();
	for (size_t i = 0; i < overlays.size(); i++) {
		auto &o = overlays[i];
		QString icon;
		if (o.type == "image" || o.type == "image_rotation")
			icon = "[IMG]";
		else if (o.type == "url")
			icon = "[URL]";
		else if (o.type == "scorecard")
			icon = "[SC]";

		QString status = o.enabled ? "ON" : "OFF";
		QString desc;
		if (o.type == "image_rotation")
			desc = QString(" (%1 images, every %2s)")
				       .arg(o.rotationImagePaths.size())
				       .arg(o.rotationIntervalSec);

		QString text = QString("%1 %2 [%3]%4").arg(icon, QString::fromStdString(o.name), status, desc);
		overlayList->addItem(text);
	}
}

void CricNodeOverlayManager::CreateOrUpdateSource(const CricNodeOverlay &overlay)
{
	OBSBasic *main = OBSBasic::Get();
	OBSScene scene = main->GetCurrentScene();
	if (!scene)
		return;

	std::string srcName = "CricNode_" + overlay.id;

	/* Remove existing source first */
	RemoveSourceForOverlay(overlay.id);

	if (overlay.type == "image") {
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_string(settings, "file", overlay.imagePath.c_str());
		OBSSourceAutoRelease src = obs_source_create("image_source", srcName.c_str(), settings, nullptr);
		if (src) {
			obs_sceneitem_t *item = obs_scene_add(scene, src);
			if (item)
				obs_sceneitem_set_visible(item, overlay.enabled);
		}

	} else if (overlay.type == "image_rotation") {
		/* Use OBS slideshow source for image rotation */
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_string(settings, "playback_behavior", "always_play");
		obs_data_set_string(settings, "slide_mode", "mode_auto");
		obs_data_set_int(settings, "slide_time", overlay.rotationIntervalSec * 1000);
		obs_data_set_string(settings, "transition", "cut");
		obs_data_set_int(settings, "transition_speed", 0);

		OBSDataArrayAutoRelease files = obs_data_array_create();
		for (auto &path : overlay.rotationImagePaths) {
			OBSDataAutoRelease item = obs_data_create();
			obs_data_set_string(item, "value", path.c_str());
			obs_data_array_push_back(files, item);
		}
		obs_data_set_array(settings, "files", files);

		OBSSourceAutoRelease src =
			obs_source_create("slideshow", srcName.c_str(), settings, nullptr);
		if (src) {
			obs_sceneitem_t *item = obs_scene_add(scene, src);
			if (item)
				obs_sceneitem_set_visible(item, overlay.enabled);
		}

	} else if (overlay.type == "url") {
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_string(settings, "url", overlay.url.c_str());
		obs_data_set_int(settings, "width", 1920);
		obs_data_set_int(settings, "height", 1080);
		obs_data_set_bool(settings, "css_override", false);

		OBSSourceAutoRelease src =
			obs_source_create("browser_source", srcName.c_str(), settings, nullptr);
		if (src) {
			obs_sceneitem_t *item = obs_scene_add(scene, src);
			if (item)
				obs_sceneitem_set_visible(item, overlay.enabled);
		}

	} else if (overlay.type == "scorecard") {
		std::string url = GetScorecardUrl(overlay);

		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_string(settings, "url", url.c_str());
		obs_data_set_int(settings, "width", 1920);
		obs_data_set_int(settings, "height", 1080);
		obs_data_set_bool(settings, "css_override", false);

		/* Inject CricNodeBridge JS for metadata collection */
		std::string customCss = "body { background-color: rgba(0,0,0,0); }";
		obs_data_set_string(settings, "css", customCss.c_str());

		OBSSourceAutoRelease src =
			obs_source_create("browser_source", srcName.c_str(), settings, nullptr);
		if (src) {
			obs_sceneitem_t *item = obs_scene_add(scene, src);
			if (item)
				obs_sceneitem_set_visible(item, overlay.enabled);
		}
	}
}

void CricNodeOverlayManager::RemoveSourceForOverlay(const std::string &overlayId)
{
	OBSBasic *main = OBSBasic::Get();
	OBSScene scene = main->GetCurrentScene();
	if (!scene)
		return;

	std::string srcName = "CricNode_" + overlayId;
	obs_sceneitem_t *item = obs_scene_find_source(scene, srcName.c_str());
	if (item)
		obs_sceneitem_remove(item);

	/* Also release the source */
	OBSSourceAutoRelease src = obs_get_source_by_name(srcName.c_str());
	if (src)
		obs_source_remove(src);
}

std::string CricNodeOverlayManager::GetScorecardUrl(const CricNodeOverlay &overlay)
{
	/* Build file:// URL pointing to bundled scorecard HTML */
	char *dataPath = obs_module_file("cricnode/scorecards");
	std::string basePath;

	if (dataPath) {
		basePath = dataPath;
		bfree(dataPath);
	} else {
		/* Fallback: use the frontend data directory */
		QString appDir = QCoreApplication::applicationDirPath();
		basePath = (appDir + "/data/obs-studio/cricnode/scorecards").toStdString();
	}

	std::string htmlFile;
	std::string params;

	if (overlay.scorecardProvider == "cricclubs") {
		htmlFile = basePath + "/cricclubs_scorecard.html";
		params = "?matchId=" + overlay.matchId + "&clubId=" + overlay.clubId +
			 "&team1=" + overlay.team1 + "&team2=" + overlay.team2 +
			 "&intro=1&player=1&wicket=1&innings=1&sidepanel=1&result=1";
	} else if (overlay.scorecardProvider == "playcricket") {
		htmlFile = basePath + "/playcricket_scorecard.html";
		params = "?matchId=" + overlay.matchId +
			 "&intro=1&player=1&wicket=1&innings=1&sidepanel=1&result=1";
	} else if (overlay.scorecardProvider == "playhq") {
		htmlFile = basePath + "/playhq_scorecard.html";
		params = "?gameId=" + overlay.matchId +
			 "&intro=1&player=1&wicket=1&innings=1&sidepanel=1&result=1";
	} else if (overlay.scorecardProvider == "dcl") {
		htmlFile = basePath + "/dcl_scorecard.html";
		params = "?matchId=" + overlay.matchId;
	}

	/* Convert to file:// URL */
	QString fileUrl = QUrl::fromLocalFile(QString::fromStdString(htmlFile)).toString();
	return fileUrl.toStdString() + params;
}

void CricNodeOverlayManager::UpdateRotatingImages()
{
	/* Image rotation is handled natively by OBS slideshow source,
	 * so this timer is kept as a hook for future custom rotation logic */
}

void CricNodeOverlayManager::SaveConfig()
{
	QJsonArray arr;
	for (auto &o : overlays)
		arr.append(o.toJson());

	QJsonObject root;
	root["overlays"] = arr;

	QJsonDocument doc(root);
	QString configDir =
		QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/cricnode";
	QDir().mkpath(configDir);

	QFile file(configDir + "/overlays.json");
	if (file.open(QIODevice::WriteOnly))
		file.write(doc.toJson());
}

void CricNodeOverlayManager::LoadConfig()
{
	QString configDir =
		QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/cricnode";
	QFile file(configDir + "/overlays.json");
	if (!file.open(QIODevice::ReadOnly))
		return;

	QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
	QJsonObject root = doc.object();
	QJsonArray arr = root["overlays"].toArray();

	overlays.clear();
	for (auto v : arr)
		overlays.push_back(CricNodeOverlay::fromJson(v.toObject()));

	RefreshList();
}

void CricNodeOverlayManager::OnFixtureWentLive(QString overlayId,
						QString newMatchId,
						QString provider)
{
	/* Find the overlay and update its match ID + browser source URL */
	for (auto &o : overlays) {
		if (o.id == overlayId.toStdString() && o.type == "scorecard") {
			o.matchId = newMatchId.toStdString();

			/* Rebuild the browser source with the new URL */
			CreateOrUpdateSource(o);
			SaveConfig();

			blog(LOG_INFO,
			     "[CricNode] Overlay %s updated to live match %s (%s)",
			     o.name.c_str(), newMatchId.toUtf8().constData(),
			     provider.toUtf8().constData());

			RefreshList();
			emit OverlaysChanged();
			break;
		}
	}
}

void CricNodeOverlayManager::ApplyOverlaysToScene()
{
	for (auto &o : overlays) {
		if (o.enabled)
			CreateOrUpdateSource(o);
	}
}
