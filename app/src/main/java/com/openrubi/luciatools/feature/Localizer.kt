package com.openrubi.luciatools.feature

import android.content.Context
import org.json.JSONObject

/** 装备数据本地化：英文 key → 中文，ClassLV → 稀有度。 */
object Localizer {
    private var loc: JSONObject? = null

    private val PROP_KEY = mapOf(
        "AttackValue" to "UI_PropertyAttack", "AttackRate" to "UI_PropertyAttackRate",
        "HPValue" to "UI_PropertyHP", "HPRate" to "UI_PropertyHPRate",
        "DefenceValue" to "UI_PropertyDefence", "DefenceRate" to "UI_PropertyDefenceRate",
        "CriticalRate" to "UI_PropertyCritical", "CriticalDamageRate" to "UI_PropertyCriticalDamage",
        "EffectHitRate" to "UI_PropertyEffectHit", "ResistanceRate" to "UI_PropertyResistance",
        "SpeedValue" to "UI_PropertySpeed", "PinchRate" to "UI_PropertyPinch",
        "CounterRate" to "UI_PropertyCounter"
    )

    private val PART_CN = mapOf(
        "Weapon" to "武器", "Body" to "防具", "Head" to "头盔",
        "Necklace" to "项链", "Ring" to "戒指", "Shoes" to "鞋子", "All" to "全部", "-" to "—"
    )

    private val RARITY = arrayOf("一般", "高级", "稀有", "史诗", "传说", "轮回")

    fun ensureLoaded(ctx: Context) {
        if (loc != null) return
        loc = try {
            val txt = ctx.assets.open("loc_equip.json").bufferedReader().use { it.readText() }
            JSONObject(txt)
        } catch (e: Exception) {
            JSONObject()
        }
    }

    private fun tr(key: String, def: String = key): String = loc?.optString(key, def) ?: def

    fun itemName(staticId: String): String =
        tr("T_Item_Name_$staticId", "($staticId)")

    fun setName(set: String): String =
        if (set.isEmpty()) "" else tr("T_EquipmentSet_Name_$set", set)

    fun part(p: String): String = PART_CN[p] ?: p

    fun rarity(classLv: Int): String =
        if (classLv in RARITY.indices) RARITY[classLv] else "?($classLv)"

    /** "AttackRate=0.11" → "攻击力 11.0%"; "HPValue=499" → "生命力 499" */
    fun prop(raw: String): String {
        if (raw.isEmpty()) return ""
        val eq = raw.indexOf('=')
        if (eq < 0) return raw
        val type = raw.substring(0, eq)
        val valStr = raw.substring(eq + 1)
        val cn = tr(PROP_KEY[type] ?: type, type).replace("(%)", "").trim()
        val d = valStr.toDoubleOrNull() ?: return "$cn $valStr"
        return if (type.endsWith("Rate")) {
            // 百分比属性不保留小数(四舍五入到整数)
            String.format("%s %d%%", cn, Math.round(d * 100.0))
        } else {
            if (d == Math.floor(d)) "$cn ${d.toLong()}" else "$cn $d"
        }
    }
}
