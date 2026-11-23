module OrdersHelper
  def money(x)
    format("%.2f", x)
  end

  def unused_money(x)
    money(x * 100)
  end
end
