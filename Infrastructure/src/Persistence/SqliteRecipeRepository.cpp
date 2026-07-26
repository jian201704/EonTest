#include "eon/infra/SqliteRecipeRepository.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace eon::infra {

SqliteRecipeRepository::SqliteRecipeRepository(const QString& dbPath)
    : dbPath_(dbPath)
    , connName_(QString("eon-recipe-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)))
{
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName_);
    db.setDatabaseName(dbPath_);
    if (!db.open()) { qCritical() << "SqliteRecipeRepository: Cannot open" << dbPath_; return; }

    QSqlQuery q(db);
    q.exec("PRAGMA journal_mode=WAL");
    q.exec("PRAGMA foreign_keys=ON");

    QString err;
    if (!ensureSchema(err)) { qCritical() << "Schema:" << err; return; }
    initialized_ = true;
}

SqliteRecipeRepository::~SqliteRecipeRepository() {
    if (QSqlDatabase::contains(connName_)) {
        { QSqlDatabase db = QSqlDatabase::database(connName_); if (db.isOpen()) db.close(); }
        QSqlDatabase::removeDatabase(connName_);
    }
}

bool SqliteRecipeRepository::isOpen() const { return initialized_; }

bool SqliteRecipeRepository::ensureSchema(QString& errorMessage) {
    QSqlDatabase db = QSqlDatabase::database(connName_);
    QSqlQuery q(db);

    const QStringList ddl = {
        "CREATE TABLE IF NOT EXISTS param_templates ("
        "  id TEXT PRIMARY KEY, name TEXT NOT NULL, workflow_id TEXT, step_id TEXT,"
        "  version TEXT DEFAULT '1.0', parameters_json TEXT NOT NULL DEFAULT '[]',"
        "  created_at TEXT DEFAULT (datetime('now')))",

        "CREATE TABLE IF NOT EXISTS recipes ("
        "  id TEXT PRIMARY KEY, name TEXT NOT NULL, template_id TEXT,"
        "  workflow_id TEXT, version TEXT DEFAULT '1.0', description TEXT,"
        "  param_values_json TEXT NOT NULL DEFAULT '{}',"
        "  created_at TEXT DEFAULT (datetime('now')))",

        "CREATE TABLE IF NOT EXISTS recipe_versions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT, recipe_id TEXT NOT NULL,"
        "  version TEXT NOT NULL, created_at TEXT DEFAULT (datetime('now')),"
        "  change_log TEXT, snapshot_json TEXT NOT NULL DEFAULT '{}')",

        "CREATE INDEX IF NOT EXISTS idx_pt_workflow ON param_templates(workflow_id)",
        "CREATE INDEX IF NOT EXISTS idx_r_workflow ON recipes(workflow_id)",
        "CREATE INDEX IF NOT EXISTS idx_r_template ON recipes(template_id)",
        "CREATE INDEX IF NOT EXISTS idx_rv_recipe ON recipe_versions(recipe_id)"
    };

    for (const auto& s : ddl) {
        if (!q.exec(s)) { errorMessage = q.lastError().text(); return false; }
    }
    return true;
}

// ============================================================================
// 参数模板
// ============================================================================

bool SqliteRecipeRepository::saveTemplate(const eon::domain::ParameterTemplate& tmpl,
                                           QString& errorMessage) {
    if (!initialized_) { errorMessage = "Not initialized."; return false; }
    QSqlDatabase db = QSqlDatabase::database(connName_);
    QSqlQuery q(db);

    QJsonArray paramsArr;
    for (const auto& p : tmpl.parameters()) paramsArr.append(QJsonObject::fromVariantMap(p.toVariantMap()));

    q.prepare("INSERT OR REPLACE INTO param_templates(id, name, workflow_id, step_id, version, parameters_json)"
              " VALUES(?,?,?,?,?,?)");
    q.addBindValue(tmpl.id()); q.addBindValue(tmpl.name());
    q.addBindValue(tmpl.workflowId()); q.addBindValue(tmpl.stepId());
    q.addBindValue(tmpl.version());
    q.addBindValue(QString::fromUtf8(QJsonDocument(paramsArr).toJson(QJsonDocument::Compact)));

    if (!q.exec()) { errorMessage = q.lastError().text(); return false; }
    return true;
}

bool SqliteRecipeRepository::findTemplateById(const QString& id, eon::domain::ParameterTemplate& tmpl,
                                                QString& errorMessage) {
    if (!initialized_) { errorMessage = "Not initialized."; return false; }
    QSqlDatabase db = QSqlDatabase::database(connName_);
    QSqlQuery q(db);
    q.prepare("SELECT * FROM param_templates WHERE id=?");
    q.addBindValue(id);
    if (!q.exec() || !q.next()) { errorMessage = "Not found."; return false; }
    tmpl = rowToTemplate(q);
    return true;
}

QList<eon::domain::ParameterTemplate> SqliteRecipeRepository::findTemplatesByWorkflow(
    const QString& workflowId, QString* errorMessage) {
    QList<eon::domain::ParameterTemplate> list;
    if (!initialized_) { if (errorMessage) *errorMessage = "Not initialized."; return list; }
    QSqlDatabase db = QSqlDatabase::database(connName_);
    QSqlQuery q(db);
    q.prepare("SELECT * FROM param_templates WHERE workflow_id=? ORDER BY name");
    q.addBindValue(workflowId);
    if (!q.exec()) { if (errorMessage) *errorMessage = q.lastError().text(); return list; }
    while (q.next()) list.append(rowToTemplate(q));
    return list;
}

QList<eon::domain::ParameterTemplate> SqliteRecipeRepository::findAllTemplates(QString* errorMessage) {
    QList<eon::domain::ParameterTemplate> list;
    if (!initialized_) { if (errorMessage) *errorMessage = "Not initialized."; return list; }
    QSqlDatabase db = QSqlDatabase::database(connName_);
    QSqlQuery q(db);
    if (!q.exec("SELECT * FROM param_templates ORDER BY name")) return list;
    while (q.next()) list.append(rowToTemplate(q));
    return list;
}

bool SqliteRecipeRepository::deleteTemplate(const QString& id, QString& errorMessage) {
    if (!initialized_) { errorMessage = "Not initialized."; return false; }
    QSqlDatabase db = QSqlDatabase::database(connName_);
    QSqlQuery q(db);
    q.prepare("DELETE FROM param_templates WHERE id=?");
    q.addBindValue(id);
    if (!q.exec()) { errorMessage = q.lastError().text(); return false; }
    return true;
}

// ============================================================================
// 配方
// ============================================================================

bool SqliteRecipeRepository::saveRecipe(const eon::domain::Recipe& recipe, QString& errorMessage) {
    if (!initialized_) { errorMessage = "Not initialized."; return false; }
    QSqlDatabase db = QSqlDatabase::database(connName_);
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO recipes(id, name, template_id, workflow_id, version, description, param_values_json)"
              " VALUES(?,?,?,?,?,?,?)");
    q.addBindValue(recipe.id()); q.addBindValue(recipe.name());
    q.addBindValue(recipe.templateId()); q.addBindValue(recipe.workflowId());
    q.addBindValue(recipe.version()); q.addBindValue(recipe.description());
    q.addBindValue(QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(recipe.parameterValues()))
                                     .toJson(QJsonDocument::Compact)));
    if (!q.exec()) { errorMessage = q.lastError().text(); return false; }

    // 保存版本快照
    eon::domain::RecipeVersion rv;
    rv.recipeId = recipe.id(); rv.version = recipe.version();
    rv.createdAt = QDateTime::currentDateTimeUtc();
    rv.changeLog = "Saved";
    rv.snapshot = recipe.parameterValues();
    return saveRecipeVersion(rv, errorMessage);
}

bool SqliteRecipeRepository::findRecipeById(const QString& id, eon::domain::Recipe& recipe,
                                              QString& errorMessage) {
    if (!initialized_) { errorMessage = "Not initialized."; return false; }
    QSqlDatabase db = QSqlDatabase::database(connName_);
    QSqlQuery q(db);
    q.prepare("SELECT * FROM recipes WHERE id=?");
    q.addBindValue(id);
    if (!q.exec() || !q.next()) { errorMessage = "Not found."; return false; }
    recipe = rowToRecipe(q);
    return true;
}

QList<eon::domain::Recipe> SqliteRecipeRepository::findRecipesByWorkflow(const QString& workflowId,
                                                                           QString* errorMessage) {
    QList<eon::domain::Recipe> list;
    if (!initialized_) { if (errorMessage) *errorMessage = "Not initialized."; return list; }
    QSqlDatabase db = QSqlDatabase::database(connName_);
    QSqlQuery q(db);
    q.prepare("SELECT * FROM recipes WHERE workflow_id=? ORDER BY created_at DESC");
    q.addBindValue(workflowId);
    if (!q.exec()) { if (errorMessage) *errorMessage = q.lastError().text(); return list; }
    while (q.next()) list.append(rowToRecipe(q));
    return list;
}

QList<eon::domain::Recipe> SqliteRecipeRepository::findRecipesByTemplate(const QString& templateId,
                                                                           QString* errorMessage) {
    QList<eon::domain::Recipe> list;
    if (!initialized_) { if (errorMessage) *errorMessage = "Not initialized."; return list; }
    QSqlDatabase db = QSqlDatabase::database(connName_);
    QSqlQuery q(db);
    q.prepare("SELECT * FROM recipes WHERE template_id=? ORDER BY created_at DESC");
    q.addBindValue(templateId);
    if (!q.exec()) { if (errorMessage) *errorMessage = q.lastError().text(); return list; }
    while (q.next()) list.append(rowToRecipe(q));
    return list;
}

QList<eon::domain::Recipe> SqliteRecipeRepository::findAllRecipes(QString* errorMessage) {
    QList<eon::domain::Recipe> list;
    if (!initialized_) { if (errorMessage) *errorMessage = "Not initialized."; return list; }
    QSqlDatabase db = QSqlDatabase::database(connName_);
    QSqlQuery q(db);
    if (!q.exec("SELECT * FROM recipes ORDER BY created_at DESC")) return list;
    while (q.next()) list.append(rowToRecipe(q));
    return list;
}

bool SqliteRecipeRepository::deleteRecipe(const QString& id, QString& errorMessage) {
    if (!initialized_) { errorMessage = "Not initialized."; return false; }
    QSqlDatabase db = QSqlDatabase::database(connName_);
    QSqlQuery q(db);
    q.prepare("DELETE FROM recipes WHERE id=?");
    q.addBindValue(id);
    if (!q.exec()) { errorMessage = q.lastError().text(); return false; }
    return true;
}

// ============================================================================
// 配方版本
// ============================================================================

bool SqliteRecipeRepository::saveRecipeVersion(const eon::domain::RecipeVersion& version,
                                                 QString& errorMessage) {
    if (!initialized_) { errorMessage = "Not initialized."; return false; }
    QSqlDatabase db = QSqlDatabase::database(connName_);
    QSqlQuery q(db);
    q.prepare("INSERT INTO recipe_versions(recipe_id, version, change_log, snapshot_json)"
              " VALUES(?,?,?,?)");
    q.addBindValue(version.recipeId); q.addBindValue(version.version);
    q.addBindValue(version.changeLog);
    q.addBindValue(QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(version.snapshot))
                                     .toJson(QJsonDocument::Compact)));
    if (!q.exec()) { errorMessage = q.lastError().text(); return false; }
    return true;
}

QList<eon::domain::RecipeVersion> SqliteRecipeRepository::findVersionsByRecipe(
    const QString& recipeId, QString* errorMessage) {
    QList<eon::domain::RecipeVersion> list;
    if (!initialized_) { if (errorMessage) *errorMessage = "Not initialized."; return list; }
    QSqlDatabase db = QSqlDatabase::database(connName_);
    QSqlQuery q(db);
    q.prepare("SELECT * FROM recipe_versions WHERE recipe_id=? ORDER BY created_at DESC");
    q.addBindValue(recipeId);
    if (!q.exec()) { if (errorMessage) *errorMessage = q.lastError().text(); return list; }
    while (q.next()) {
        eon::domain::RecipeVersion rv;
        rv.recipeId = q.value("recipe_id").toString();
        rv.version = q.value("version").toString();
        rv.createdAt = QDateTime::fromString(q.value("created_at").toString(), Qt::ISODate);
        rv.changeLog = q.value("change_log").toString();
        rv.snapshot = QJsonDocument::fromJson(q.value("snapshot_json").toString().toUtf8()).object().toVariantMap();
        list.append(rv);
    }
    return list;
}

QVariantMap SqliteRecipeRepository::getRecipeSnapshot(const QString& recipeId, const QString& version,
                                                       QString& errorMessage) {
    if (!initialized_) { errorMessage = "Not initialized."; return {}; }
    QSqlDatabase db = QSqlDatabase::database(connName_);
    QSqlQuery q(db);
    q.prepare("SELECT snapshot_json FROM recipe_versions WHERE recipe_id=? AND version=? ORDER BY created_at DESC LIMIT 1");
    q.addBindValue(recipeId); q.addBindValue(version);
    if (!q.exec() || !q.next()) { errorMessage = "Version not found."; return {}; }
    return QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object().toVariantMap();
}

// ============================================================================
// 内部映射
// ============================================================================

eon::domain::ParameterTemplate SqliteRecipeRepository::rowToTemplate(const QSqlQuery& q) const {
    eon::domain::ParameterTemplate t;
    t.setId(q.value("id").toString());
    t.setName(q.value("name").toString());
    t.setWorkflowId(q.value("workflow_id").toString());
    t.setStepId(q.value("step_id").toString());
    t.setVersion(q.value("version").toString());

    const QJsonArray arr = QJsonDocument::fromJson(q.value("parameters_json").toString().toUtf8()).array();
    QList<eon::domain::ParameterDef> params;
    for (const auto& v : arr) {
        const QJsonObject o = v.toObject();
        eon::domain::ParameterDef p;
        p.name = o.value("name").toString();
        p.displayName = o.value("displayName").toString();
        p.description = o.value("description").toString();
        p.type = eon::domain::paramTypeFromString(o.value("type").toString());
        p.defaultValue = o.value("defaultValue").toVariant();
        p.minValue = o.value("minValue").toVariant();
        p.maxValue = o.value("maxValue").toVariant();
        p.unit = o.value("unit").toString();
        p.required = o.value("required").toBool();
        for (const auto& ev : o.value("enumValues").toArray())
            p.enumValues.append(ev.toString());
        params.append(p);
    }
    t.setParameters(params);
    return t;
}

eon::domain::Recipe SqliteRecipeRepository::rowToRecipe(const QSqlQuery& q) const {
    eon::domain::Recipe r;
    r.setId(q.value("id").toString());
    r.setName(q.value("name").toString());
    r.setTemplateId(q.value("template_id").toString());
    r.setWorkflowId(q.value("workflow_id").toString());
    r.setVersion(q.value("version").toString());
    r.setDescription(q.value("description").toString());
    r.setCreatedAt(QDateTime::fromString(q.value("created_at").toString(), Qt::ISODate));
    r.setParameterValues(
        QJsonDocument::fromJson(q.value("param_values_json").toString().toUtf8()).object().toVariantMap());
    return r;
}

} // namespace eon::infra
