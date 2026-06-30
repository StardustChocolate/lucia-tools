package com.openrubi.luciatools.feature

import org.json.JSONArray

/**
 * 装备强度打分引擎(参数与解析)。
 * 统一评分公式见 StatCalc。
 */
object StatScorer {

    /** ===== 可调常量 ===== */
    const val MIN_STRENGTHEN = 15            // 强化阈值(满强化=+15)
    const val MIN_EQUIP_LV = 75              // 装备等级阈值(75级以上,含75)

    /** 范数 P 值：平衡峰值与数量。P 越大越偏向峰值(单件极值)，P 越小越偏向数量(累积)。默认 2。 */
    const val NORM_P = 2.0

    /** 雷达满分刻度(每个轴映射到 0~10) */
    const val RADAR_SCALE = 10.0

    /** 每个维度的参数：百分比基础 / 加数 / 是否剔除项链主属性为该维 / 满分阈值(范数=该值时记满分10) */
    data class DimParam(
        val key: String, val label: String,
        val pctBase: Double, val add: Double,
        val excludeNecklaceMain: Boolean,
        val fullThreshold: Double   // 该维满分阈值：列表范数=此值映射为满分10(每维可单独调)
    )

    // 维度定义(顺序即雷达图轴顺序)
    val DIMS = listOf(
        DimParam("attack",    "攻击",     989.0,  1228.0,  true,  45000.0),
        DimParam("hp",        "生命",     5364.0, 6266.76, false, 300000.0),
        DimParam("defence",   "防御",     648.0,  687.0,   false, 30000.0),
        DimParam("critRate",  "暴击率",   100.0,  15.0,    true,  1000.0),
        DimParam("critDmg",   "暴击伤害", 100.0,  150.0,   false, 3000.0),
        DimParam("effectHit", "状态命中", 100.0,  0.0,     false, 1000.0),
        DimParam("resist",    "状态抗性", 100.0,  0.0,     false, 1000.0),
        DimParam("speed",     "速度",     121.0,  125.0,   false, 2800.0)
    )

    fun dim(key: String) = DIMS.first { it.key == key }

    // 部位(与 native PART 一致)
    val PARTS = listOf("Weapon", "Body", "Head", "Necklace", "Ring", "Shoes")

    /** 维度 -> (固定属性名, 百分比属性名)；速度无百分比则 rate 为空 */
    val PROP_OF = mapOf(
        "attack" to ("AttackValue" to "AttackRate"),
        "hp" to ("HPValue" to "HPRate"),
        "defence" to ("DefenceValue" to "DefenceRate"),
        "critRate" to ("" to "CriticalRate"),
        "critDmg" to ("" to "CriticalDamageRate"),
        "effectHit" to ("" to "EffectHitRate"),
        "resist" to ("" to "ResistanceRate"),
        "speed" to ("SpeedValue" to "")
    )

    /** 一件装备 */
    data class Equip(
        val staticId: String, val part: String, val equipLv: Int,
        val strengthen: Int, val classLv: Int, val set: String,
        val mainProps: List<Pair<String, Double>>,
        val subProps: List<Pair<String, Double>>
    ) {
        val allProps: List<Pair<String, Double>> get() = mainProps + subProps
    }

    fun parse(json: String): List<Equip> {
        val arr = JSONArray(json)
        val list = ArrayList<Equip>(arr.length())
        for (i in 0 until arr.length()) {
            val o = arr.getJSONObject(i)
            val main = parseProp(o.optString("mainProp"))
            val subs = ArrayList<Pair<String, Double>>()
            val sa = o.optJSONArray("subProps") ?: JSONArray()
            for (j in 0 until sa.length()) parseProp(sa.getString(j))?.let { subs.add(it) }
            list.add(
                Equip(
                    staticId = o.optString("staticId"),
                    part = o.optString("part"),
                    equipLv = o.optInt("equipLv"),
                    strengthen = o.optInt("strengthen"),
                    classLv = o.optInt("classLv"),
                    set = o.optString("set"),
                    mainProps = if (main != null) listOf(main) else emptyList(),
                    subProps = subs
                )
            )
        }
        return list
    }

    private fun parseProp(raw: String): Pair<String, Double>? {
        if (raw.isEmpty()) return null
        val eq = raw.indexOf('='); if (eq < 0) return null
        val type = raw.substring(0, eq)
        val v = raw.substring(eq + 1).toDoubleOrNull() ?: return null
        return type to v
    }
}
