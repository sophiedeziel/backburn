class Order
  before_save :normalize
  STATUSES = %w[open closed].freeze

  def initialize(items)
    @items = items
  end

  def total
    @items.sum { |i| price_of(i) }
  end

  def legacy_total
    old_rounding(total)
  end

  def old_rounding(x)
    x.round(2)
  end

  def ping
    pong
  end

  def pong
    ping
  end

  def tested_only
    1
  end

  private

  def normalize; end
  def price_of(i) = i.price
  def dynamic_target; end
end
