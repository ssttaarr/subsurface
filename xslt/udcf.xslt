<?xml version="1.0"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:strip-space elements="*"/>
  <xsl:include href="commonTemplates.xsl"/>
  <xsl:output method="xml" indent="yes"/>

  <xsl:template match="/">
    <divelog program="subsurface-import" version="2">
      <settings>
        <divecomputerid deviceid="ffffffff">
          <xsl:apply-templates select="/PROFILE/DEVICE|/profile/device"/>
        </divecomputerid>
      </settings>
      <dives>
	      <xsl:apply-templates select="/PROFILE/REPGROUP/DIVE|/profile/repgroup/dive"/>
      </dives>
    </divelog>
  </xsl:template>

  <xsl:template match="DEVICE|device">
    <xsl:if test="MODEL|model != ''">
      <xsl:attribute name="model">
        <xsl:value-of select="MODEL|model"/>
      </xsl:attribute>
    </xsl:if>
    <xsl:if test="version|VERSION != ''">
      <xsl:attribute name="serial">
        <xsl:value-of select="VERSION|version"/>
      </xsl:attribute>
    </xsl:if>
  </xsl:template>

  <xsl:template match="DIVE|dive">
    <xsl:variable name="units">
      <xsl:choose>
        <xsl:when test="translate(//units|//UNITS, 'ABCDEFGHIJKLMNOPQRSTUVWXYZ', 'abcdefghijklmnopqrstuvwxyz') = 'imperial'">
          <xsl:value-of select="'Imperial'"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="'Metric'"/>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
    <dive>
      <xsl:attribute name="date">
        <xsl:for-each select="DATE/YEAR|DATE/MONTH|DATE/DAY|date/year|date/month|date/day">
          <xsl:if test="position() != 1">-</xsl:if>
          <xsl:value-of select="."/>
        </xsl:for-each>
      </xsl:attribute>

      <xsl:attribute name="time">
        <xsl:for-each select="TIME/HOUR|TIME/MINUTE|time/hour|time/minute">
          <xsl:if test="position() != 1">:</xsl:if>
          <xsl:value-of select="."/>
        </xsl:for-each>
      </xsl:attribute>

      <location>
        <xsl:value-of select="PLACE|place"/>
      </location>

      <xsl:if test="TEMPERATURE|temperature != ''">
        <temperature>
          <xsl:attribute name="water">
            <xsl:call-template name="tempConvert">
              <xsl:with-param name="temp">
                <xsl:value-of select="TEMPERATURE|temperature"/>
              </xsl:with-param>
              <xsl:with-param name="units" select="$units"/>
            </xsl:call-template>
          </xsl:attribute>
        </temperature>
      </xsl:if>

      <divecomputer deviceid="ffffffff">
        <xsl:attribute name="model">
          <xsl:value-of select="/PROFILE/DEVICE/MODEL|/profile/device/model"/>
        </xsl:attribute>
      </divecomputer>

      <xsl:for-each select="GASES/MIX|gases/mix">
        <cylinder>
          <xsl:attribute name="description">
            <xsl:value-of select="MIXNAME|mixname"/>
          </xsl:attribute>
          <xsl:attribute name="size">
            <xsl:choose>
              <xsl:when test="$units = 'Imperial'">
                <xsl:value-of select="concat(TANK/TANKVOLUME|tank/tankvolume div 7, ' l')"/>
              </xsl:when>
              <xsl:otherwise>
                <xsl:value-of select="concat(TANK/TANKVOLUME|tank/tankvolume, ' l')"/>
              </xsl:otherwise>
            </xsl:choose>
          </xsl:attribute>
          <xsl:attribute name="start">
            <xsl:call-template name="pressureConvert">
              <xsl:with-param name="number">
                <xsl:value-of select="TANK/PSTART|tank/pstart"/>
              </xsl:with-param>
              <xsl:with-param name="units" select="$units"/>
            </xsl:call-template>
          </xsl:attribute>
          <xsl:attribute name="end">
            <xsl:call-template name="pressureConvert">
              <xsl:with-param name="number">
                <xsl:value-of select="TANK/PEND|tank/pend"/>
              </xsl:with-param>
              <xsl:with-param name="units" select="$units"/>
            </xsl:call-template>
          </xsl:attribute>
          <xsl:attribute name="o2">
            <xsl:value-of select="O2|o2"/>
          </xsl:attribute>
          <xsl:attribute name="he">
            <xsl:value-of select="HE|he"/>
          </xsl:attribute>
        </cylinder>
      </xsl:for-each>

      <xsl:choose>

	<!-- samples recorded at irregular internal, but storing time stamp -->
	<xsl:when test="timedepthmode">
          <xsl:variable name="timeconvert">
            <xsl:choose>
              <xsl:when test="//units = 'si'">
                <xsl:value-of select="60"/>
              </xsl:when>
              <xsl:otherwise>
                <xsl:value-of select="1"/>
              </xsl:otherwise>
            </xsl:choose>
          </xsl:variable>
          <debug name="{$timeconvert}"/>
	  <!-- gas change -->
	  <xsl:for-each select="SAMPLES/SWITCH|samples/switch">
	    <event name="gaschange">
	      <xsl:variable name="timeSec" select="following-sibling::T|following-sibling::t"/>
	      <xsl:attribute name="time">
		<xsl:value-of select="concat(floor($timeSec div $timeconvert), ':',
		  format-number(floor($timeSec mod $timeconvert), '00'), ' min')"/>
	      </xsl:attribute>
	      <xsl:attribute name="value">
		<xsl:value-of select="ancestor::DIVE/GASES/MIX[MIXNAME=current()]/O2|ancestor::dive/gases/mix[mixname=current()]/o2 * 100" />
	      </xsl:attribute>
	    </event>
	  </xsl:for-each>
	  <!-- end gas change -->

	  <!-- samples -->
	  <xsl:for-each select="SAMPLES/D|samples/d">
	    <sample>
	      <xsl:variable name="timeSec" select="preceding-sibling::T[position()=1]|preceding-sibling::t[position()=1]"/>
	      <xsl:attribute name="time">
		<xsl:value-of select="concat(floor($timeSec div $timeconvert), ':',
		  format-number(floor($timeSec mod $timeconvert), '00'), ' min')"/>
	      </xsl:attribute>
	      <xsl:attribute name="depth">
                <xsl:call-template name="depthConvert">
                  <xsl:with-param name="depth">
                    <xsl:value-of select="."/>
                  </xsl:with-param>
                  <xsl:with-param name="units" select="$units"/>
                </xsl:call-template>
	      </xsl:attribute>
	    </sample>
	  </xsl:for-each>
	  <!-- end samples -->
	</xsl:when>

	<!-- sample recorded at even internals -->
	<xsl:otherwise>
	  <xsl:variable name="delta" select="SAMPLES/DELTA|samples/delta"/>

	  <!-- gas change -->
	  <xsl:for-each select="SAMPLES/SWITCH|samples/switch">
	    <event name="gaschange">
	      <xsl:variable name="timeSec" select="count(preceding-sibling::D|preceding-sibling::d) * $delta"/>
	      <xsl:attribute name="time">
		<xsl:value-of select="concat(floor($timeSec div 60), ':',
		  format-number(floor($timeSec mod 60), '00'), ' min')"/>
	      </xsl:attribute>
	      <xsl:attribute name="value">
		<xsl:value-of select="ancestor::DIVE/GASES/MIX[MIXNAME=current()]/O2|ancestor::dive/gases/mix[mixname=current()]/o2 * 100" />
	      </xsl:attribute>
	    </event>

	  </xsl:for-each>
	  <!-- end gas change -->

	  <!-- samples -->
	  <xsl:for-each select="SAMPLES/D|samples/d">
	    <sample>
	      <xsl:variable name="timeSec" select="(position() - 1) * $delta"/>
	      <xsl:attribute name="time">
		<xsl:value-of select="concat(floor($timeSec div 60), ':',
		  format-number(floor($timeSec mod 60), '00'), ' min')"/>
	      </xsl:attribute>
	      <xsl:attribute name="depth">
                <xsl:call-template name="depthConvert">
                  <xsl:with-param name="depth">
                    <xsl:value-of select="."/>
                  </xsl:with-param>
                  <xsl:with-param name="units" select="$units"/>
                </xsl:call-template>
	      </xsl:attribute>
	    </sample>
	  </xsl:for-each>
	  <!-- end samples -->

	</xsl:otherwise>
      </xsl:choose>
    </dive>
  </xsl:template>
</xsl:stylesheet>
